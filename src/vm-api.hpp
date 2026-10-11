// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <memory>
#include <cstddef>
#include <cstdint>
namespace linkaudio {
// ABI and mappings verified against the official Voicemeeter-SDK revision
// 02a1abf15358ddb33e588cd31c43576a065f81ac, VoicemeeterRemote.h.
struct VmAudioInfo {
    long rate, frames;
};
struct VmAudioBuffer {
    long rate, frames, inputs, outputs;
    float *read[128];
    float *write[128];
};
static_assert(sizeof(long) == 4 && offsetof(VmAudioBuffer, read) == 16);
using VmCallback = long(__stdcall *)(void *, long, void *, long);
constexpr long vmStarting = 1, vmEnding = 2, vmChange = 3, vmBufferIn = 10, vmBufferOut = 11;
enum class CallbackTarget { strips = 0, buses = 1 };
inline bool singleDestination(uint32_t mask) {
    return mask && !(mask & (mask - 1));
}
struct VmInfo {
    int type = 0;
    QString version, message = "Detecting VoiceMeeter…";
    bool running() const {
        return type >= 1 && type <= 3;
    }
};
struct VmLayout {
    int strips = 0, hardware = 0, buses = 0;
    static VmLayout forType(int type) {
        if (type == 1)
            return {3, 2, 1};
        if (type == 2)
            return {5, 3, 3};
        if (type == 3)
            return {8, 5, 5};
        return {};
    }
    int channel(CallbackTarget target, int index) const {
        if (target == CallbackTarget::buses)
            return index * 8;
        return index < hardware ? index * 2 : hardware * 2 + (index - hardware) * 8;
    }
    int count(CallbackTarget target) const {
        return target == CallbackTarget::strips ? strips : buses;
    }
};
inline QString vmEdition(int type) {
    return type == 3   ? "VoiceMeeter Potato"
           : type == 2 ? "VoiceMeeter Banana"
           : type == 1 ? "VoiceMeeter Standard"
                       : "VoiceMeeter";
}
// A test double implements the same documented DLL boundary; no audio settings
// setters or application-launch entry points are exposed here.
class VmApi {
  public:
    virtual ~VmApi() = default;
    virtual bool load(QString &error) = 0;
    virtual long login() = 0;
    virtual long logout() = 0;
    virtual long type(long *) = 0;
    virtual long version(long *) = 0;
    virtual long registerCallback(long, VmCallback, void *, char *) = 0;
    virtual long start() = 0;
    virtual long stop() = 0;
    virtual long unregisterCallback() = 0;
};
std::unique_ptr<VmApi> makeVmApi();
} // namespace linkaudio
