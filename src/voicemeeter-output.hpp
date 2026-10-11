// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "callback-audio.hpp"
#include <QElapsedTimer>
#include <vector>
struct SpeexResamplerState_;
namespace linkaudio {
struct CallbackStats {
    uint64_t calls = 0, frames = 0, underruns = 0, dropped = 0;
    uint32_t rate = 0;
    int queuedMs = 0;
    QString detail;
};
// All methods run on the network worker. Only CallbackAudio enters the realtime thread.
class VoiceMeeterOutput {
    std::unique_ptr<VmApi> api;
    std::unique_ptr<CallbackAudio> audio;
    bool loggedIn = false, registered = false;
    int edition = 0, bufferMs = 40;
    SpeexResamplerState_ *resampler = nullptr;
    uint32_t sourceRate = 0, outputRate = 0;
    uint64_t receivedFrames = 0, lastCalls = 0;
    QElapsedTimer health, heartbeat;
    std::vector<float> converted;
    QString failure;
    VmInfo info();
    void resetResampler();

  public:
    explicit VoiceMeeterOutput(std::unique_ptr<VmApi> api = makeVmApi());
    ~VoiceMeeterOutput();
    VmInfo detect();
    bool start(int type, CallbackTarget target, uint32_t mask, int buffer, QString &error);
    bool stop(QString &error);
    void flush();
    void feed(const float *samples, size_t frames, uint32_t rate);
    CallbackStats poll();
};
} // namespace linkaudio
