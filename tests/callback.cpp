// SPDX-License-Identifier: GPL-2.0-or-later
#include "network.hpp"
#include "mock-vm.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QNetworkDatagram>
#include <iostream>
#include <stdexcept>
using namespace linkaudio;
static void check(bool yes, const char *message) {
    if (!yes)
        throw std::runtime_error(message);
}
template <class F> static bool until(F f, int ms = 4000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents();
        if (f())
            return true;
        QThread::msleep(1);
    }
    return f();
}
static void mappings() {
    const std::array<std::vector<int>, 3> strips{{{0, 2, 4}, {0, 2, 4, 6, 14}, {0, 2, 4, 6, 8, 10, 18, 26}}};
    const int buses[] = {1, 3, 5};
    std::array<float, 16384> samples;
    for (size_t f = 0; f < samples.size() / 2; ++f) {
        samples[f * 2] = .125f;
        samples[f * 2 + 1] = -.25f;
    }
    for (int type = 1; type <= 3; ++type)
        for (auto target : {CallbackTarget::strips, CallbackTarget::buses}) {
            const int count =
                target == CallbackTarget::strips ? int(strips[type - 1].size()) : buses[type - 1];
            for (int selection = 0; selection <= count; ++selection) {
                const uint32_t mask = selection == count ? (1u << count) - 1 : 1u << selection;
                auto audio = std::make_unique<CallbackAudio>(type, target, mask, 20);
                auto vm = std::make_unique<MockVm>();
                vm->active = true;
                vm->callback = CallbackAudio::callback;
                vm->user = audio.get();
                vm->mode = target == CallbackTarget::strips ? 1 : 2;
                vm->render();
                check(audio->push(samples.data(), samples.size() / 2), "queue accepts audio");
                // This deliberately overfills the latency bound: keep only recent target audio.
                vm->render();
                vm->render();
                for (int c = 0; c < (vm->mode == 1 ? 34 : 64); ++c) {
                    float added = 0;
                    for (int i = 0; i < count; ++i)
                        if (mask & (1u << i)) {
                            int channel = target == CallbackTarget::strips ? strips[type - 1][i] : i * 8;
                            if (c == channel)
                                added = .125f;
                            if (c == channel + 1)
                                added = -.25f;
                        }
                    check(std::abs(vm->output[c][255] - (vm->input[c][255] + added)) < 1e-5,
                          "exact strip/bus mapping; existing and B audio preserved");
                }
                audio->flush();
                vm->render();
                for (int c = 0; c < (vm->mode == 1 ? 34 : 64); ++c)
                    check(vm->output[c] == vm->input[c], "disconnect flush removes return on every channel");
            }
        }
}
static void lifecycle() {
    auto fake = std::make_unique<MockVm>();
    auto *vm = fake.get();
    VoiceMeeterOutput output(std::move(fake));
    QString error;
    check(output.detect().type == 3 && vm->logins == 0, "detection does not keep a login");
    vm->registerResult = 1;
    check(!output.start(3, CallbackTarget::buses, 1, 40, error) && error.contains("Other callback app"),
          "busy owner explained");
    check(vm->stops == 0 && vm->unregisters == 0 && vm->logins == 0, "never stop or unregister another app");
    vm->registerResult = 0;
    check(!output.start(2, CallbackTarget::strips, 1, 40, error), "edition change blocks stale destinations");
    check(!output.start(3, CallbackTarget::buses, 32, 40, error), "B1 is not selectable");
    vm->startResult = -1;
    check(!output.start(3, CallbackTarget::buses, 1, 40, error) && !vm->callback && vm->logins == 0,
          "failed start releases owned registration");
    vm->startResult = 0;
    check(output.start(3, CallbackTarget::strips, 255, 40, error), "all Potato strips start");
    vm->render();
    vm->callback(vm->user, vmChange, nullptr, 0);
    check(output.poll().detail.contains("engine changed"), "engine change requires explicit reconnect");
    vm->unregisterResult = -1;
    check(!output.stop(error) && vm->callback, "failed unregister retains callback storage");
    vm->unregisterResult = 0;
    check(output.stop(error) && !vm->callback && vm->logins == 0, "owned callback released");
}
static void transport(uint32_t sourceRate, uint32_t destinationRate) {
    QTemporaryDir root;
    auto id = loadIdentity(root.path());
    SharedStatus rs, ss;
    auto fake = std::make_unique<MockVm>();
    auto *vm = fake.get();
    Receiver receiver(id, rs, std::move(fake));
    QTcpServer port;
    check(port.listen(QHostAddress::LocalHost, 0), "reserve port");
    const auto number = port.serverPort();
    port.close();
    QUdpSocket sink;
    check(sink.bind(QHostAddress::LocalHost, 0), "UDP observer");
    ReceiverConfig config{"Callback test", "AudioLink", number, sink.localPort(), 40};
    config.mode = OutputMode::callback;
    config.target = CallbackTarget::buses;
    config.destinations = 17;
    config.vmType = 3;
    QString error;
    check(receiver.begin(config, error, false), qPrintable(error));
    AudioQueue audio;
    Sender sender(audio, ss);
    sender.start();
    sender.configure({"127.0.0.1", id.pairing(), number, true, 60});
    check(until([&] { return ss.get().connected; }), "old protocol sender pairs in callback mode");
    std::array<float, 256> samples;
    for (int i = 0; i < 128; ++i) {
        samples[i * 2] = .125f;
        samples[i * 2 + 1] = -.25f;
    }
    QElapsedTimer clock;
    clock.start();
    qint64 produced = 0, consumed = 0;
    int audible = 0;
    until(
        [&] {
            auto elapsed = clock.nsecsElapsed();
            while (produced + 128 <= elapsed * sourceRate / 1000000000) {
                audio.push(samples.data(), 128, sourceRate);
                produced += 128;
            }
            while (consumed + 256 <= elapsed * destinationRate / 1000000000) {
                vm->render(destinationRate);
                consumed += 256;
                if (std::abs(vm->output[0][255] - .135f) < .003 &&
                    std::abs(vm->output[33][255] - .09f) < .003)
                    ++audible;
                check(vm->output[40] == vm->input[40], "B1 passes through during transport");
                check(vm->output[8] == vm->input[8], "unselected A2 passes through");
            }
            return clock.elapsed() >= 1600;
        },
        2200);
    const auto measured = rs.get();
    std::cout << "Rate " << sourceRate << " -> " << destinationRate << ": audible blocks=" << audible
              << ", delivered=" << measured.callbackFrames << ", received=" << measured.received
              << ", queue=" << measured.queuedMs << ", A1L=" << vm->output[0][255]
              << ", A5R=" << vm->output[33][255] << ", " << measured.detail.toStdString() << '\n';
    check(audible > 100 && measured.callbackFrames > destinationRate,
          "stereo arrives through TLS, conversion and selected A1/A5 callbacks");
    check(rs.get().sent == 0 && !sink.hasPendingDatagrams(), "callback mode sends no VBAN UDP");
    check(ss.get().received > 100, "existing sender receives compatible acknowledgements");
    sender.stop();
    until([&] { return !rs.get().connected; });
    vm->render(destinationRate);
    check(vm->output[0] == vm->input[0], "disconnect clears callback return without muting existing audio");
    config.mode = OutputMode::vban;
    check(receiver.begin(config, error, false) && !vm->callback, "switching to VBAN releases callback");
    Sender resumed(audio, ss);
    resumed.start();
    resumed.configure({"127.0.0.1", id.pairing(), number, true, 60});
    check(until([&] { return ss.get().connected; }), "sender reconnects after mode switch");
    for (int i = 0; i < 64; ++i)
        audio.push(samples.data(), 128, sourceRate);
    check(until([&] { return sink.hasPendingDatagrams(); }), "VBAN output works after callback mode");
    resumed.stop();
    receiver.stop();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--probe")) {
        VoiceMeeterOutput output;
        auto info = output.detect();
        std::cout << "Read-only API detection: " << info.message.toStdString() << "\n";
        return 0;
    }
    try {
        mappings();
        lifecycle();
        transport(48000, 44100);
        transport(44100, 48000);
        std::cout << "PASS: all strip/A bus mappings, multi-select, preservation, API lifecycle, legacy TLS "
                     "sender, rate conversion, disconnect and VBAN switch\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
