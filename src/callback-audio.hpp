// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "vm-api.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
namespace linkaudio {
struct StereoFrame {
    float left = 0, right = 0;
};
// Network worker is the sole producer; VoiceMeeter's non-reentrant callback is
// the sole consumer. Clearing uses a generation, never writes the other's cursor.
class CallbackAudio {
    static constexpr uint64_t capacity = 262144;
    std::array<StereoFrame, capacity> samples{};
    alignas(64) std::atomic<uint64_t> write{0};
    alignas(64) std::atomic<uint64_t> read{0};
    std::atomic<uint64_t> generation{1};
    uint64_t observedGeneration = 0;
    VmLayout layout;
    CallbackTarget target;
    uint32_t mask;
    int bufferMs;
    bool primed = false;
    float fade = 0;
    std::atomic<float> requestedGain{1};
    float currentGain = 1;
    static_assert(std::atomic<float>::is_always_lock_free, "Audio gain must not lock in the callback");
    float gainForDb(int db) const {
        return target == CallbackTarget::strips ? 1.0f
               : db <= -61                      ? 0.0f
                                                : std::pow(10.0f, std::clamp(db, -60, 0) / 20.0f);
    }

  public:
    std::atomic<bool> enabled{true}, restartNeeded{false}, invalidBuffer{false};
    std::atomic<uint32_t> rate{0};
    std::atomic<uint32_t> quantum{0};
    std::atomic<uint64_t> calls{0}, delivered{0}, underruns{0}, dropped{0};
    CallbackAudio(int type, CallbackTarget t, uint32_t selected, int buffer, int busGainDb = 0)
        : layout(VmLayout::forType(type)), target(t), mask(selected), bufferMs(buffer) {
        currentGain = gainForDb(busGainDb);
        requestedGain.store(currentGain, std::memory_order_relaxed);
    }
    // Called by the worker. Convert dB here, never in the realtime callback.
    void setBusGainDb(int db) {
        requestedGain.store(gainForDb(db), std::memory_order_relaxed);
    }
    size_t queued() const {
        const auto r = read.load(std::memory_order_acquire);
        const auto w = write.load(std::memory_order_acquire);
        return size_t(std::min<uint64_t>(capacity, w - r));
    }
    void flush() {
        generation.fetch_add(1, std::memory_order_release);
    }
    bool push(const float *interleaved, size_t count) {
        const auto w = write.load(std::memory_order_relaxed);
        if (count > capacity - std::min<uint64_t>(capacity, w - read.load(std::memory_order_acquire))) {
            dropped.fetch_add(count, std::memory_order_relaxed);
            return false;
        }
        for (size_t i = 0; i < count; ++i)
            samples[(w + i) % capacity] = {interleaved[i * 2], interleaved[i * 2 + 1]};
        write.store(w + count, std::memory_order_release);
        return true;
    }
    static long __stdcall callback(void *user, long command, void *data, long) {
        return static_cast<CallbackAudio *>(user)->process(command, data);
    }
    long process(long command, void *data) noexcept {
        if (command == vmEnding || command == vmChange) {
            rate.store(0, std::memory_order_release);
            restartNeeded.store(true, std::memory_order_release);
            return 0;
        }
        if (command == vmStarting)
            return 0; // Actual buffer supplies authoritative format.
        if (command != (target == CallbackTarget::strips ? vmBufferIn : vmBufferOut) || !data)
            return 0;
        auto &b = *static_cast<VmAudioBuffer *>(data);
        if (b.frames < 1 || b.frames > 8192 || b.inputs < 0 || b.inputs > 128 || b.outputs < 0 ||
            b.outputs > 128) {
            invalidBuffer.store(true, std::memory_order_release);
            return 0;
        }
        // Callback owns the complete insert: pass through every channel first,
        // including all B buses and unused surround channels.
        for (int c = 0; c < b.outputs; ++c) {
            if (!b.write[c])
                continue;
            if (c < b.inputs && b.read[c]) {
                if (b.write[c] != b.read[c])
                    std::memmove(b.write[c], b.read[c], size_t(b.frames) * sizeof(float));
            } else
                std::memset(b.write[c], 0, size_t(b.frames) * sizeof(float));
        }
        calls.fetch_add(1, std::memory_order_relaxed);
        if (!enabled.load(std::memory_order_acquire) || restartNeeded.load(std::memory_order_acquire))
            return 0;
        if (b.rate < 6000 || b.rate > 768000) {
            invalidBuffer.store(true);
            return 0;
        }
        for (int i = 0; i < layout.count(target); ++i)
            if (mask & (1u << i)) {
                const int c = layout.channel(target, i);
                if (c + 1 >= b.outputs || !b.write[c] || !b.write[c + 1]) {
                    invalidBuffer.store(true);
                    return 0;
                }
            }
        if (rate.exchange(uint32_t(b.rate), std::memory_order_acq_rel) != uint32_t(b.rate))
            flush();
        quantum.store(uint32_t(b.frames), std::memory_order_relaxed);
        const auto epoch = generation.load(std::memory_order_acquire);
        auto r = read.load(std::memory_order_relaxed);
        const auto w = write.load(std::memory_order_acquire);
        if (observedGeneration != epoch) {
            observedGeneration = epoch;
            r = w;
            primed = false;
            fade = 0;
        }
        const auto goal = std::max<uint64_t>(uint64_t(b.frames) * 2, uint64_t(b.rate) * bufferMs / 1000);
        if (w - r > goal + uint64_t(b.rate) / 10) {
            dropped.fetch_add(w - r - goal, std::memory_order_relaxed);
            r = w - goal;
            primed = false;
            fade = 0;
        }
        if (!primed && w - r >= goal)
            primed = true;
        uint64_t count = 0;
        const float targetGain = requestedGain.load(std::memory_order_relaxed);
        const float gainStep = 100.0f / float(b.rate); // At most 10 ms for a full-scale move.
        if (primed)
            for (int f = 0; f < b.frames; ++f) {
                if (r == w) {
                    primed = false;
                    fade = 0;
                    underruns.fetch_add(1);
                    break;
                }
                auto sample = samples[r++ % capacity];
                fade = std::min(1.0f, fade + 200.0f / float(b.rate));
                currentGain += std::clamp(targetGain - currentGain, -gainStep, gainStep);
                const float returnGain = fade * currentGain;
                for (int i = 0; i < layout.count(target); ++i)
                    if (mask & (1u << i)) {
                        const int c = layout.channel(target, i);
                        b.write[c][f] += sample.left * returnGain;
                        b.write[c + 1][f] += sample.right * returnGain;
                    }
                ++count;
            }
        read.store(r, std::memory_order_release);
        delivered.fetch_add(count, std::memory_order_relaxed);
        return 0;
    }
};
} // namespace linkaudio
