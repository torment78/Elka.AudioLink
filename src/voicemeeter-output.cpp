// SPDX-License-Identifier: GPL-2.0-or-later
#include "voicemeeter-output.hpp"
#include <speex/speex_resampler.h>
namespace linkaudio {
VoiceMeeterOutput::VoiceMeeterOutput(std::unique_ptr<VmApi> a) : api(std::move(a)), converted(65536 * 2) {}
VoiceMeeterOutput::~VoiceMeeterOutput() {
    QString error;
    if (!stop(error)) {
        // A failed unregister may leave the DLL calling us. Keep disabled pass-through
        // and its DLL alive until process exit, rather than freeing a live callback.
        audio.release();
        api.release();
    }
}
VmInfo VoiceMeeterOutput::info() {
    long type = 0, version = 0;
    if (api->type(&type) != 0 || type < 1 || type > 3)
        return {0, {}, "VoiceMeeter is not running. Start it, then click Detect."};
    QString text;
    if (api->version(&version) == 0) {
        auto v = uint32_t(version);
        text =
            QString("%1.%2.%3.%4").arg((v >> 24) & 255).arg((v >> 16) & 255).arg((v >> 8) & 255).arg(v & 255);
    }
    return {int(type), text, vmEdition(int(type)) + " " + text};
}
VmInfo VoiceMeeterOutput::detect() {
    QString error;
    if (!api->load(error))
        return {0, {}, error};
    if (loggedIn)
        return info();
    const auto result = api->login();
    if (result < 0)
        return {0, {}, "Cannot connect to the VoiceMeeter API (" + QString::number(result) + ")."};
    auto found =
        result == 1 ? VmInfo{0, {}, "VoiceMeeter is not running. Start it, then click Detect."} : info();
    api->logout();
    return found;
}
bool VoiceMeeterOutput::start(int type, CallbackTarget target, uint32_t mask, int buffer, QString &error,
                              int busGainDb) {
    if (!stop(error))
        return false;
    failure.clear();
    const int count = VmLayout::forType(type).count(target);
    if (!count || !singleDestination(mask) || (mask >> count) || buffer < 20 || buffer > 200 ||
        busGainDb < -61 || busGainDb > 0) {
        error = "Detect VoiceMeeter, pick one channel only, and use a return level from Mute to 0 dB.";
        return false;
    }
    if (!api->load(error))
        return false;
    const auto result = api->login();
    if (result < 0) {
        error = "Cannot log in to VoiceMeeter (" + QString::number(result) + ").";
        return false;
    }
    loggedIn = true;
    auto found =
        result == 1 ? VmInfo{0, {}, "VoiceMeeter is not running. Start it, then click Detect."} : info();
    if (found.type != type) {
        error = found.running() ? "VoiceMeeter edition changed. Detect again and reselect destinations."
                                : found.message;
        QString ignored;
        stop(ignored);
        return false;
    }
    edition = type;
    bufferMs = buffer;
    audio = std::make_unique<CallbackAudio>(type, target, mask, buffer, busGainDb);
    char name[64] = "Elka AudioLink";
    const auto rc = api->registerCallback(target == CallbackTarget::strips ? 1 : 2, CallbackAudio::callback,
                                          audio.get(), name);
    name[63] = 0;
    if (rc != 0) {
        error = rc == 1 ? "This callback is already in use by " + QString::fromLocal8Bit(name) +
                              ". Release it in that app, or use VBAN."
                        : "Cannot register the VoiceMeeter callback (" + QString::number(rc) + ").";
        QString ignored;
        stop(ignored);
        return false;
    }
    registered = true;
    const auto started = api->start();
    if (started != 0) {
        error = "Cannot start the VoiceMeeter callback (" + QString::number(started) + ").";
        QString ignored;
        stop(ignored);
        return false;
    }
    health.start();
    heartbeat.start();
    lastCalls = 0;
    return true;
}
void VoiceMeeterOutput::resetResampler() {
    if (resampler)
        speex_resampler_destroy(resampler);
    resampler = nullptr;
    sourceRate = outputRate = 0;
    receivedFrames = 0;
}
bool VoiceMeeterOutput::stop(QString &error) {
    resetResampler();
    if (audio)
        audio->enabled.store(false, std::memory_order_release);
    if (registered) {
        api->stop();
        const auto rc = api->unregisterCallback();
        if (rc != 0 && rc != 1) {
            error = "VoiceMeeter could not release the callback. AudioLink is passing existing audio "
                    "through. Restart VoiceMeeter, then retry.";
            failure = error;
            return false;
        }
        registered = false;
    }
    audio.reset();
    if (loggedIn) {
        api->logout();
        loggedIn = false;
    }
    return true;
}
void VoiceMeeterOutput::flush() {
    if (audio)
        audio->flush();
    resetResampler();
}
void VoiceMeeterOutput::feed(const float *samples, size_t frames, uint32_t rate) {
    if (!audio || !failure.isEmpty() || !audio->enabled.load() || audio->restartNeeded.load())
        return;
    const auto destinationRate = audio->rate.load(std::memory_order_acquire);
    if (!destinationRate || !frames || frames > 128 || rate < 6000 || rate > 768000)
        return;
    if (rate != sourceRate || destinationRate != outputRate) {
        flush();
        int error = 0;
        resampler = speex_resampler_init(2, rate, destinationRate, 5, &error);
        if (!resampler || error) {
            failure = "Cannot create the audio resampler. Stop and start reception.";
            return;
        }
        sourceRate = rate;
        outputRate = destinationRate;
    }
    // Slowly correct independent sound-card clocks, bounded to 0.2%. Runs only
    // on the worker; the callback merely consumes preconverted stereo frames.
    receivedFrames += frames;
    if (receivedFrames >= sourceRate / 2) {
        receivedFrames = 0;
        const double goal = std::max(double(outputRate) * bufferMs / 1000, double(audio->quantum.load()) * 2);
        const double correction =
            std::clamp((double(audio->queued()) - goal) / std::max(1.0, goal) * 0.001, -0.002, 0.002);
        // Integer-Hz numerator gives ~21 ppm steps at 48 kHz and keeps Speex's
        // interpolation arithmetic bounded even for extreme supported rates.
        const auto numerator = uint32_t(std::llround(double(sourceRate) * (1 + correction)));
        if (speex_resampler_set_rate_frac(resampler, numerator, outputRate, sourceRate, outputRate)) {
            failure = "Audio rate adjustment failed. Stop and start reception.";
            audio->flush();
            return;
        }
    }
    spx_uint32_t in = spx_uint32_t(frames), out = spx_uint32_t(converted.size() / 2);
    const auto result =
        speex_resampler_process_interleaved_float(resampler, samples, &in, converted.data(), &out);
    if (result || in != frames) {
        failure = "Audio conversion failed. Stop and start reception.";
        audio->flush();
        return;
    }
    audio->push(converted.data(), out);
}
CallbackStats VoiceMeeterOutput::poll() {
    CallbackStats s;
    if (!audio) {
        s.detail = failure;
        return s;
    }
    if (health.isValid() && health.elapsed() >= 1000 && failure.isEmpty()) {
        health.restart();
        auto found = info();
        if (found.type != edition)
            failure = "VoiceMeeter stopped or its edition changed. Detect and apply again.";
    }
    if (audio->restartNeeded.load())
        failure = "VoiceMeeter's audio engine changed. Click Start / apply or tray Refresh to reconnect.";
    if (audio->invalidBuffer.load())
        failure =
            "VoiceMeeter supplied an unsupported channel layout. Stop reception and update VoiceMeeter.";
    if (!failure.isEmpty())
        audio->enabled.store(false, std::memory_order_release);
    s.calls = audio->calls.load();
    s.frames = audio->delivered.load();
    s.rate = audio->rate.load();
    s.underruns = audio->underruns.load();
    s.dropped = audio->dropped.load();
    s.queuedMs = s.rate ? int(audio->queued() * 1000 / s.rate) : 0;
    if (s.calls != lastCalls) {
        heartbeat.restart();
        lastCalls = s.calls;
    }
    s.detail = !failure.isEmpty() ? failure
               : !s.calls || heartbeat.elapsed() > 2000
                   ? "Waiting for VoiceMeeter audio callbacks. Check that its audio engine is running."
                   : "Direct callback active. Return mixes into the selected channel; check VoiceMeeter's "
                     "meters and output routing.";
    return s;
}
} // namespace linkaudio
