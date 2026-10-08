#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/message_text.hpp"
#include "reboot/ux/suggested_action.hpp"

namespace reboot::ux {

// Persisted by name; renaming is a migration.
enum class NoticeKind : u8 {
    // While an unlisted host session is Live; never persisted.
    UnlistedLive,
};

[[nodiscard]] std::string_view persisted_name(NoticeKind kind);
[[nodiscard]] std::optional<NoticeKind> parse_notice_kind(std::string_view name);

// UnlistedLive is keyed by session.
struct NoticeKey {
    NoticeKind kind{};
    std::optional<SessionId> session;

    bool operator==(const NoticeKey&) const = default;
};

// Capabilities: discoverable-default.
// An info bar that stays until dismissed or until its condition ends, never a timed toast.
struct Notice {
    NoticeKey key;
    Severity severity = Severity::Info;
    std::chrono::system_clock::time_point created_at;
    MessageText title;
    MessageText body;
    std::vector<SuggestedAction> actions;
    bool dismissible = true;
};

// Published as EventKind::NoticeAdded.
struct NoticeAddedEvent {
    Notice notice;
};

// Published as EventKind::NoticeRemoved when a notice is dismissed or its condition ends.
struct NoticeRemovedEvent {
    NoticeKey key;
};

}  // namespace reboot::ux
