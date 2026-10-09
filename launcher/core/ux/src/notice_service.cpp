#include "reboot/ux/notice_service.hpp"

#include <algorithm>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/ux/guidance_state_store.hpp"
#include "messages.hpp"

namespace reboot::ux {

namespace {

// Session-scoped kinds live in NoticeService's memory and are never read back from the store.
[[nodiscard]] bool is_session_scoped(NoticeKind kind) {
    switch (kind) {
        case NoticeKind::UnlistedLive: return true;
    }
    return false;
}

struct KindText {
    MessageId title;
    MessageId body;
};

[[nodiscard]] KindText text_of(NoticeKind kind) {
    switch (kind) {
        case NoticeKind::UnlistedLive: return {msg::kNoticeUnlistedLiveTitle, msg::kNoticeUnlistedLiveBody};
    }
    return {msg::kNoticeUnlistedLiveTitle, msg::kNoticeUnlistedLiveBody};
}

[[nodiscard]] Notice one_time_notice(const OneTimeNoticeRecord& record) {
    const KindText text = text_of(record.key.kind);
    return Notice{
        .key = record.key,
        .created_at = record.created_at,
        .title = {text.title, record.args},
        .body = {text.body, record.args},
    };
}

[[nodiscard]] Notice unlisted_live_notice(SessionId session, HostProfileId profile,
                                         std::chrono::system_clock::time_point created_at) {
    const KindText text = text_of(NoticeKind::UnlistedLive);
    return Notice{
        .key = {NoticeKind::UnlistedLive, session},
        .created_at = created_at,
        .title = {text.title, {}},
        .body = {text.body, {}},
        .actions = {CopyShareLink{session}, ListHostProfile{profile}},
    };
}

}  // namespace

Diagnostic to_diagnostic(const NoticeError& error) {
    switch (error.code) {
        case NoticeErrorCode::UnknownNotice:
            return make_diag(ErrorDomain::Ux, msg::kUnknownNotice)
                .arg("notice", persisted_name(error.key.kind))
                .kind(ErrorKind::NotFound);
        case NoticeErrorCode::NotDismissible:
            return make_diag(ErrorDomain::Ux, msg::kNoticeNotDismissible)
                .arg("notice", persisted_name(error.key.kind))
                .kind(ErrorKind::InvalidInput);
    }
    return internal_bug("ux::to_diagnostic(NoticeError)");
}

std::vector<Notice> NoticeService::list() const {
    std::vector<Notice> out;
    for (const OneTimeNoticeRecord& record : store_.current().notices)
        if (!record.dismissed && !is_session_scoped(record.key.kind)) out.push_back(one_time_notice(record));
    for (const SessionBanner& banner : unlisted_live_)
        if (!banner.dismissed) out.push_back(unlisted_live_notice(banner.session, banner.profile, banner.created_at));
    std::ranges::stable_sort(out, {}, &Notice::created_at);
    return out;
}

Result<void> NoticeService::dismiss(const NoticeKey& key) {
    const auto unknown = [&key] {
        return std::unexpected(to_diagnostic(NoticeError{NoticeErrorCode::UnknownNotice, key}));
    };
    if (is_session_scoped(key.kind)) {
        const auto banner = std::ranges::find_if(unlisted_live_, [&key](const SessionBanner& b) {
            return key.session == b.session && !b.dismissed;
        });
        if (banner == unlisted_live_.end()) return unknown();
        banner->dismissed = true;
    } else {
        GuidanceState next = store_.current();
        const auto record = std::ranges::find_if(
            next.notices, [&key](const OneTimeNoticeRecord& r) { return r.key == key && !r.dismissed; });
        if (record == next.notices.end()) return unknown();
        record->dismissed = true;
        if (Result<void> written = store_.replace(std::move(next)); !written) return written;
    }
    events_.publish(EventKind::NoticeRemoved, NoticeRemovedEvent{key}, EventScope{.session = key.session});
    return {};
}

void NoticeService::update_unlisted_live(SessionId session, HostProfileId profile, bool unlisted_and_live) {
    const auto banner =
        std::ranges::find_if(unlisted_live_, [&session](const SessionBanner& b) { return b.session == session; });
    if (!unlisted_and_live) {
        if (banner == unlisted_live_.end()) return;
        const bool shown = !banner->dismissed;
        unlisted_live_.erase(banner);
        if (shown)
            events_.publish(EventKind::NoticeRemoved, NoticeRemovedEvent{{NoticeKind::UnlistedLive, session}},
                            EventScope{.session = session});
        return;
    }
    std::chrono::system_clock::time_point created_at = clock_.system_now();
    if (banner != unlisted_live_.end()) {
        // A changed profile republishes the notice so its List publicly action targets the new one.
        if (banner->profile == profile) return;
        banner->profile = profile;
        if (banner->dismissed) return;
        created_at = banner->created_at;
    } else {
        unlisted_live_.push_back(SessionBanner{session, profile, created_at});
    }
    events_.publish(EventKind::NoticeAdded, NoticeAddedEvent{unlisted_live_notice(session, profile, created_at)},
                    EventScope{.session = session});
}

}  // namespace reboot::ux
