#include "reboot/logging/log_line_forwarder.hpp"

#include <atomic>
#include <utility>

#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"

namespace rb::logging {

namespace {

constexpr std::size_t kDrainPageEntries = 256;

}  // namespace

struct LogLineForwarder::Impl {
    // Shared with the ring callback and posted drains, which may outlive the forwarder.
    struct Link {
        std::atomic<bool> drain_posted{false};
        // Strand-only; null once the forwarder is gone.
        Impl* owner = nullptr;
    };

    Impl(LogRing& ring_ref, EventBus& events_ref) : ring(ring_ref), events(events_ref), cursor(ring_ref.end()) {}

    void drain() {
        const LogFilter everything;
        for (;;) {
            LogPage page = ring.read(cursor, everything, kDrainPageEntries);
            if (page.next == cursor) return;
            cursor = page.next;
            for (LogEntry& entry : page.entries) {
                EventScope scope;
                scope.session = entry.record.session;
                events.publish(EventKind::LogLine, LogLineEvent{std::move(entry)}, std::move(scope));
            }
        }
    }

    LogRing& ring;
    EventBus& events;
    LogCursor cursor;
    std::shared_ptr<Link> link = std::make_shared<Link>();
};

LogLineForwarder::LogLineForwarder(LogRing& ring, EventBus& events, Executor& strand)
    : impl_(std::make_unique<Impl>(ring, events)) {
    impl_->link->owner = impl_.get();
    ring.set_on_append([link = impl_->link, &strand] {
        if (link->drain_posted.exchange(true)) return;
        strand.post([link] {
            link->drain_posted.store(false);
            if (link->owner) link->owner->drain();
        });
    });
}

LogLineForwarder::~LogLineForwarder() {
    impl_->ring.set_on_append({});
    impl_->link->owner = nullptr;
}

}  // namespace rb::logging
