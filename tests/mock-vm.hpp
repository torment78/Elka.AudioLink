// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "vm-api.hpp"
#include <array>
#include <cstring>
struct MockVm : linkaudio::VmApi {
    int edition = 3, unregisters = 0, stops = 0, logins = 0;
    long registerResult = 0, startResult = 0, unregisterResult = 0;
    bool active = false;
    long mode = 0;
    linkaudio::VmCallback callback = nullptr;
    void *user = nullptr;
    std::array<std::array<float, 256>, 64> input{}, output{};
    bool load(QString &) override {
        return true;
    }
    long login() override {
        ++logins;
        return edition ? 0 : 1;
    }
    long logout() override {
        --logins;
        return 0;
    }
    long type(long *value) override {
        *value = edition;
        return edition ? 0 : -2;
    }
    long version(long *value) override {
        *value = 0x03020100;
        return 0;
    }
    long registerCallback(long m, linkaudio::VmCallback cb, void *p, char *name) override {
        if (registerResult) {
            std::strcpy(name, "Other callback app");
            return registerResult;
        }
        mode = m;
        callback = cb;
        user = p;
        return 0;
    }
    long start() override {
        if (!startResult)
            active = true;
        return startResult;
    }
    long stop() override {
        ++stops;
        active = false;
        return 0;
    }
    long unregisterCallback() override {
        ++unregisters;
        if (!unregisterResult) {
            active = false;
            callback = nullptr;
            user = nullptr;
        }
        return unregisterResult;
    }
    void render(uint32_t rate = 48000) {
        for (int c = 0; c < 64; ++c) {
            input[c].fill(.01f * (c + 1));
            output[c].fill(-10);
        }
        if (!active || !callback)
            return;
        linkaudio::VmAudioBuffer b{};
        b.rate = rate;
        b.frames = 256;
        b.inputs = b.outputs = mode == 1 ? 34 : 64;
        for (int c = 0; c < b.outputs; ++c) {
            b.read[c] = input[c].data();
            b.write[c] = output[c].data();
        }
        callback(user, mode == 1 ? linkaudio::vmBufferIn : linkaudio::vmBufferOut, &b, 0);
    }
};
