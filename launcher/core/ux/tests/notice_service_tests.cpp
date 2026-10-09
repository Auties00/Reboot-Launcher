#include <chrono>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/ux/notice_service.hpp"
#include "test_support.hpp"

using namespace reboot;
using namespace reboot::ux;
using namespace std::chrono_literals;

namespace {

struct Fixture {
    test::MemoryGuidanceStore store;
    EventBus bus{EngineEpoch{1}};
    testing::EventRecorder recorder{bus};
    ManualClock clock;
    NoticeService notices{store, bus, clock};

    SessionId session = test::make_id<SessionId>(1);
    SessionId other_session = test::make_id<SessionId>(2);
    HostProfileId profile = test::make_id<HostProfileId>(3);

    [[nodiscard]] std::vector<NoticeAddedEvent> added() {
        recorder.pump();
        std::vector<NoticeAddedEvent> out;
        for (const NoticeAddedEvent* e : recorder.payloads<NoticeAddedEvent>(EventKind::NoticeAdded)) out.push_back(*e);
        return out;
    }
    [[nodiscard]] std::vector<NoticeKey> removed() {
        recorder.pump();
        std::vector<NoticeKey> out;
        for (const NoticeRemovedEvent* e : recorder.payloads<NoticeRemovedEvent>(EventKind::NoticeRemoved))
            out.push_back(e->key);
        return out;
    }
};

}  // namespace

TEST_CASE("persisted names of notice kinds") {
    CHECK(persisted_name(NoticeKind::UnlistedLive) == "unlisted_live");
    CHECK(parse_notice_kind("unlisted_live") == NoticeKind::UnlistedLive);
    CHECK_FALSE(parse_notice_kind("Unlisted_Live"));
    CHECK_FALSE(parse_notice_kind(""));
}

TEST_CASE("UnlistedLive shows while the session is unlisted and live") {
    Fixture f;
    f.clock.set_system(std::chrono::system_clock::time_point{100s});
    f.notices.update_unlisted_live(f.session, f.profile, true);

    const std::vector<Notice> listed = f.notices.list();
    REQUIRE(listed.size() == 1);
    const Notice& notice = listed[0];
    CHECK(notice.key == NoticeKey{NoticeKind::UnlistedLive, f.session});
    CHECK(notice.severity == Severity::Info);
    CHECK(notice.created_at == std::chrono::system_clock::time_point{100s});
    CHECK(notice.title.id.id == "ux.notice_unlisted_live_title");
    CHECK(notice.body.id.id == "ux.notice_unlisted_live_body");
    CHECK(notice.dismissible);
    REQUIRE(notice.actions.size() == 2);
    CHECK(std::get<CopyShareLink>(notice.actions[0]).session == f.session);
    CHECK(std::get<ListHostProfile>(notice.actions[1]).profile == f.profile);

    const std::vector<NoticeAddedEvent> added = f.added();
    REQUIRE(added.size() == 1);
    CHECK(added[0].notice.key == notice.key);
    REQUIRE(f.recorder.events().size() == 1);
    CHECK(f.recorder.events()[0].session == f.session);
    CHECK(f.store.writes == 0);
}

TEST_CASE("repeated updates do not republish an unchanged banner") {
    Fixture f;
    f.notices.update_unlisted_live(f.session, f.profile, true);
    f.clock.advance(5s);
    f.notices.update_unlisted_live(f.session, f.profile, true);
    CHECK(f.added().size() == 1);
    CHECK(f.notices.list().size() == 1);
    CHECK(f.notices.list()[0].created_at == std::chrono::system_clock::time_point{});
}

TEST_CASE("a changed profile republishes the banner with its new action") {
    Fixture f;
    f.notices.update_unlisted_live(f.session, f.profile, true);
    const HostProfileId next = test::make_id<HostProfileId>(9);
    f.notices.update_unlisted_live(f.session, next, true);
    const std::vector<NoticeAddedEvent> added = f.added();
    REQUIRE(added.size() == 2);
    CHECK(std::get<ListHostProfile>(added[1].notice.actions[1]).profile == next);
    CHECK(f.notices.list().size() == 1);
}

TEST_CASE("the banner goes when the condition ends") {
    Fixture f;
    f.notices.update_unlisted_live(f.session, f.profile, true);
    f.notices.update_unlisted_live(f.other_session, f.profile, true);
    f.notices.update_unlisted_live(f.session, f.profile, false);

    CHECK(f.removed() == std::vector<NoticeKey>{{NoticeKind::UnlistedLive, f.session}});
    const std::vector<Notice> listed = f.notices.list();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].key.session == f.other_session);

    f.notices.update_unlisted_live(f.session, f.profile, false);
    CHECK(f.removed().size() == 1);
}

TEST_CASE("dismissing a banner hides it until the condition ends and returns") {
    Fixture f;
    f.notices.update_unlisted_live(f.session, f.profile, true);
    const NoticeKey key{NoticeKind::UnlistedLive, f.session};

    REQUIRE(f.notices.dismiss(key));
    CHECK(f.notices.list().empty());
    CHECK(f.removed() == std::vector<NoticeKey>{key});

    f.notices.update_unlisted_live(f.session, f.profile, true);
    CHECK(f.notices.list().empty());
    CHECK(f.added().size() == 1);

    f.notices.update_unlisted_live(f.session, test::make_id<HostProfileId>(7), true);
    CHECK(f.notices.list().empty());

    f.notices.update_unlisted_live(f.session, f.profile, false);
    CHECK(f.removed().size() == 1);
    f.notices.update_unlisted_live(f.session, f.profile, true);
    CHECK(f.notices.list().size() == 1);
    CHECK(f.added().size() == 2);
}

TEST_CASE("dismissing an unknown or already dismissed notice fails") {
    Fixture f;
    const NoticeKey key{NoticeKind::UnlistedLive, f.session};
    const Result<void> unknown = f.notices.dismiss(key);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().id == "ux.unknown_notice");
    CHECK(unknown.error().kind == ErrorKind::NotFound);
    REQUIRE(unknown.error().find_arg("notice") != nullptr);
    CHECK(std::get<std::string>(*unknown.error().find_arg("notice")) == "unlisted_live");

    f.notices.update_unlisted_live(f.session, f.profile, true);
    REQUIRE_FALSE(f.notices.dismiss(NoticeKey{NoticeKind::UnlistedLive, std::nullopt}));
    REQUIRE(f.notices.dismiss(key));
    CHECK_FALSE(f.notices.dismiss(key));
    CHECK(f.removed().size() == 1);
}

TEST_CASE("session-scoped records in the store are never listed") {
    Fixture f;
    GuidanceState state;
    state.notices.push_back(OneTimeNoticeRecord{.key = {NoticeKind::UnlistedLive, f.session}});
    REQUIRE(f.store.replace(state));
    CHECK(f.notices.list().empty());
    CHECK_FALSE(f.notices.dismiss(NoticeKey{NoticeKind::UnlistedLive, f.session}));
}

TEST_CASE("NoticeError converts to its diagnostic") {
    const Diagnostic not_dismissible =
        to_diagnostic(NoticeError{NoticeErrorCode::NotDismissible, {NoticeKind::UnlistedLive, std::nullopt}});
    CHECK(not_dismissible.id == "ux.notice_not_dismissible");
    CHECK(not_dismissible.domain == ErrorDomain::Ux);
    CHECK(std::get<std::string>(*not_dismissible.find_arg("notice")) == "unlisted_live");
}
