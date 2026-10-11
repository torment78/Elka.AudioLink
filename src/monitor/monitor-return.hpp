// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "vban-transmitter.hpp"
#include <functional>
namespace vban {
class MonitorReturn {
public:
    using Sink = std::function<void(const float *, size_t, uint32_t)>;
    explicit MonitorReturn(Sink sink = {});
    void enable_tunnel(bool enabled, uint32_t buffer_ms = default_return_buffer_ms);
    ~MonitorReturn();
    void start();
    void shutdown();
    Transmitter::Prepared prepare(const ReturnConfigs &cfg, std::string &error, const std::string &local_ip = {},
                                  uint32_t buffer_ms = default_return_buffer_ms);
    void activate(Transmitter::Prepared config, uint32_t buffer_ms = default_return_buffer_ms);
    ReturnStatus status(size_t i) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
