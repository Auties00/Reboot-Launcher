#pragma once

#include <chrono>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/notice.hpp"

namespace rb {
class EventBus;
class IClock;
}  // namespace rb

namespace rb::ux {

class IGuidanceStateStore;

enum class NoticeErrorCode : u8 { UnknownNotice, NotDismissible };

struct NoticeError {
    NoticeErrorCode code{};
    NoticeKey key;
};

// The notice arg is the kind's persisted name.
[[nodiscard]] Diagnostic to_diagnostic(const NoticeError& error);

// Capabilities: discoverable-default.
// Strand-only. One-time notices persist through IGuidanceStateStore; UnlistedLive lives with its session.
class NoticeService {
public:
    NoticeService(IGuidanceStateStore& store, EventBus& events, const IClock& clock)
        : store_(store), events_(events), clock_(clock) {}

    [[nodiscard]] std::vector<Notice> list() const;
    Result<void> dismiss(const NoticeKey& key);

    // Called on every host phase or listing change; shows the banner while `unlisted_and_live`.
    void update_unlisted_live(SessionId session, HostProfileId profile, bool unlisted_and_live);

private:
    struct SessionBanner {
        SessionId session;
        HostProfileId profile;
        std::chrono::system_clock::time_point created_at;
        bool dismissed = false;
    };

    IGuidanceStateStore& store_;
    EventBus& events_;
    const IClock& clock_;
    std::vector<SessionBanner> unlisted_live_;
};

}  // namespace rb::ux
