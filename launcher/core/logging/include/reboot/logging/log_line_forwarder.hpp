#pragma once

#include <cstddef>
#include <memory>

#include "reboot/logging/log_ring.hpp"

namespace rb {
class EventBus;
class Executor;
}  // namespace rb

namespace rb::logging {

// Payload of EventKind::LogLine; the bus drops it with a counter when a subscriber lags.
struct LogLineEvent {
    LogEntry entry;

    [[nodiscard]] std::size_t approx_bytes() const noexcept { return sizeof(LogLineEvent) + entry.record.text.size(); }
};

// Capabilities: logging-diagnostics.log-and-errors.
// Strand-only. Publishes each new ring entry, with at most one drain posted however fast the writer appends.
class LogLineForwarder {
public:
    LogLineForwarder(LogRing& ring, EventBus& events, Executor& strand);
    // Unhooks from the ring; a drain already posted becomes a no-op.
    ~LogLineForwarder();
    LogLineForwarder(const LogLineForwarder&) = delete;
    LogLineForwarder& operator=(const LogLineForwarder&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::logging
