#include "reboot/ux/notice.hpp"

namespace rb::ux {

std::string_view persisted_name(NoticeKind kind) {
    switch (kind) {
        case NoticeKind::UnlistedLive: return "unlisted_live";
    }
    return {};
}

std::optional<NoticeKind> parse_notice_kind(std::string_view name) {
    if (name == "unlisted_live") return NoticeKind::UnlistedLive;
    return std::nullopt;
}

}  // namespace rb::ux
