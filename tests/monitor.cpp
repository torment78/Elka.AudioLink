// SPDX-License-Identifier: GPL-2.0-or-later
#include "monitor-return.hpp"
#include <obs.h>
#include <util/platform.h>
#include <thread>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char *m) {
    if (!ok)
        throw std::runtime_error(m);
}
static const char *name(void *) {
    return "AudioLink monitor test";
}
static void *create(obs_data_t *, obs_source_t *s) {
    return s;
}
static void destroy(void *) {}
static void output(obs_source_t *s, float value, uint64_t ts) {
    std::array<float, 960> samples;
    samples.fill(value);
    obs_source_audio audio{};
    audio.data[0] = reinterpret_cast<uint8_t *>(samples.data());
    audio.frames = 480;
    audio.samples_per_sec = 48000;
    audio.format = AUDIO_FORMAT_FLOAT;
    audio.speakers = SPEAKERS_STEREO;
    audio.timestamp = ts;
    obs_source_output_audio(s, &audio);
}
int main() {
    try {
        check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
        obs_audio_info info{};
        info.samples_per_sec = 48000;
        info.speakers = SPEAKERS_STEREO;
        check(obs_reset_audio(&info), "OBS audio init");
        obs_source_info si{};
        si.id = "audiolink_test";
        si.type = OBS_SOURCE_TYPE_INPUT;
        si.output_flags = OBS_SOURCE_AUDIO;
        si.get_name = name;
        si.create = create;
        si.destroy = destroy;
        obs_register_source(&si);
        std::atomic<float> level{0};
        std::atomic<uint64_t> blocks{0};
        {
            vban::MonitorReturn monitor([&](const float *samples, size_t frames, uint32_t rate) {
                check(frames == 128 && rate == 48000, "monitor format");
                float peak = 0;
                for (size_t i = 0; i < frames * 2; ++i)
                    peak = std::max(peak, std::abs(samples[i]));
                level = peak;
                ++blocks;
            });
            monitor.start();
            monitor.enable_tunnel(true, 60);
            auto *a = obs_source_create("audiolink_test", "A", nullptr, nullptr);
            auto *b = obs_source_create("audiolink_test", "B", nullptr, nullptr);
            check(a && b, "sources created after monitor startup");
            obs_source_inc_active(a);
            obs_source_inc_active(b);
            obs_source_set_monitoring_type(a, OBS_MONITORING_TYPE_MONITOR_ONLY);
            obs_source_set_monitoring_type(b, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
            obs_source_set_volume(a, 0.5f);
            auto drive = [&](float expected) {
                int matching = 0;
                auto start = os_gettime_ns();
                for (int i = 0; i < 90; ++i) {
                    auto ts = start + uint64_t(i) * 10000000;
                    output(a, 0.2f, ts);
                    output(b, 0.1f, ts);
                    std::this_thread::sleep_until(
                        std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ts + 10000000)));
                    if (i > 30 && std::abs(level.load() - expected) < 0.003f)
                        ++matching;
                }
                check(matching > 25, "monitor selections and fader gain reflected in return");
            };
            drive(0.2f);
            obs_source_set_monitoring_type(b, OBS_MONITORING_TYPE_NONE);
            drive(0.1f);
            obs_source_set_monitoring_type(a, OBS_MONITORING_TYPE_NONE);
            drive(0.0f);
            check(blocks > 500, "continuous monitor worker output");
            obs_source_dec_active(a);
            obs_source_dec_active(b);
            obs_source_release(a);
            obs_source_release(b);
            obs_wait_for_destroy_queue();
            monitor.shutdown();
        }
        obs_shutdown();
        std::cout << "PASS: OBS monitor-only and monitor/output mix, gain, Monitor Off, silence, source "
                     "creation/removal, clean shutdown\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        obs_shutdown();
        return 1;
    }
}
