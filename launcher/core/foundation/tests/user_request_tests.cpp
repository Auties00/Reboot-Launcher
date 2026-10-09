#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/events.hpp"
#include "reboot/foundation/user_request.hpp"

using namespace reboot;

namespace {

struct Rig {
    EventBus events{EngineEpoch{1}};
    UserRequestRegistry requests{events};
    std::shared_ptr<Subscription> sub = events.subscribe({}, 1 << 20);

    std::vector<EventEnvelope> drain() {
        std::vector<EventEnvelope> out;
        sub->drain(out, 1000);
        return out;
    }
};

Result<void> accept_any(const std::any&) { return {}; }

Result<void> want_yes(const std::any& answer) {
    const auto* text = std::any_cast<std::string>(&answer);
    if (text != nullptr && *text == "yes") return {};
    return make_diag(ErrorDomain::Requests, MessageId{"requests.invalid_answer"}).kind(ErrorKind::InvalidInput).fail();
}

}  // namespace

TEST_CASE("ask publishes UserActionRequired and lists the request as pending", "[foundation][requests]") {
    Rig rig;
    const SessionId session{Uuid{{7}}};
    const RequestId id = rig.requests.ask(UserRequestKind::ConfirmJoin, std::string("payload"), OpId{3}, session,
                                          accept_any, CancelToken{});
    const auto events = rig.drain();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == EventKind::UserActionRequired);
    CHECK(events[0].op == OpId{3});
    CHECK(events[0].session == session);
    const auto& raised = std::any_cast<const UserRequest&>(events[0].payload);
    CHECK(raised.id == id);
    CHECK(raised.kind == UserRequestKind::ConfirmJoin);
    CHECK(std::any_cast<std::string>(raised.payload) == "payload");

    const auto pending = rig.requests.pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].id == id);
}

TEST_CASE("The first valid answer wins; later answers are already_resolved", "[foundation][requests]") {
    Rig rig;
    int answered = 0;
    const RequestId id = rig.requests.ask(
        UserRequestKind::AutoServerConsent, {}, std::nullopt, std::nullopt,
        [&](const std::any& answer) {
            ++answered;
            return want_yes(answer);
        },
        CancelToken{});
    rig.drain();

    const Result<void> wrong = rig.requests.respond(id, std::string("no"));
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().kind == ErrorKind::InvalidInput);
    CHECK(rig.requests.pending().size() == 1);
    CHECK(rig.drain().empty());

    CHECK(rig.requests.respond(id, std::string("yes")));
    CHECK(rig.requests.pending().empty());
    const auto events = rig.drain();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == EventKind::UserActionResolved);
    const auto& resolved = std::any_cast<const UserActionResolvedEvent&>(events[0].payload);
    CHECK(resolved.id == id);
    CHECK(resolved.resolution == RequestResolution::Answered);

    const Result<void> late = rig.requests.respond(id, std::string("yes"));
    REQUIRE_FALSE(late);
    CHECK(late.error().id == "requests.already_resolved");
    CHECK(late.error().kind == ErrorKind::Conflict);
    CHECK(answered == 2);
}

TEST_CASE("An unknown request is not_found", "[foundation][requests]") {
    Rig rig;
    const Result<void> missing = rig.requests.respond(RequestId{42}, {});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "requests.not_found");
    CHECK(missing.error().kind == ErrorKind::NotFound);
    CHECK(rig.requests.respond(RequestId{0}, {}).error().id == "requests.not_found");
}

TEST_CASE("Cancelling the owning token withdraws the request", "[foundation][requests]") {
    Rig rig;
    CancelSource source;
    const RequestId id =
        rig.requests.ask(UserRequestKind::NeedsSecret, {}, std::nullopt, std::nullopt, accept_any, source.token());
    rig.drain();
    source.cancel(CancelReason::User);
    CHECK(rig.requests.pending().empty());
    const auto events = rig.drain();
    REQUIRE(events.size() == 1);
    CHECK(std::any_cast<const UserActionResolvedEvent&>(events[0].payload).resolution == RequestResolution::Withdrawn);
    CHECK(rig.requests.respond(id, {}).error().id == "requests.already_resolved");
}

TEST_CASE("Asking with a cancelled token raises and withdraws at once", "[foundation][requests]") {
    Rig rig;
    CancelSource source;
    source.cancel(CancelReason::Shutdown);
    rig.requests.ask(UserRequestKind::ChooseVersion, {}, std::nullopt, std::nullopt, accept_any, source.token());
    CHECK(rig.requests.pending().empty());
    const auto events = rig.drain();
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == EventKind::UserActionRequired);
    CHECK(events[1].kind == EventKind::UserActionResolved);
}

TEST_CASE("A withdrawal during validation waits for the verdict", "[foundation][requests][race]") {
    SECTION("a valid answer still wins") {
        Rig rig;
        CancelSource source;
        const RequestId id = rig.requests.ask(
            UserRequestKind::ConfirmUntested, {}, std::nullopt, std::nullopt,
            [&](const std::any&) -> Result<void> {
                source.cancel(CancelReason::User);
                return {};
            },
            source.token());
        rig.drain();
        CHECK(rig.requests.respond(id, {}));
        const auto events = rig.drain();
        REQUIRE(events.size() == 1);
        CHECK(std::any_cast<const UserActionResolvedEvent&>(events[0].payload).resolution ==
              RequestResolution::Answered);
    }
    SECTION("a rejected answer becomes a withdrawal") {
        Rig rig;
        CancelSource source;
        const RequestId id = rig.requests.ask(
            UserRequestKind::ConfirmUntested, {}, std::nullopt, std::nullopt,
            [&](const std::any& answer) {
                source.cancel(CancelReason::User);
                return want_yes(answer);
            },
            source.token());
        rig.drain();
        CHECK_FALSE(rig.requests.respond(id, std::string("no")));
        CHECK(rig.requests.pending().empty());
        const auto events = rig.drain();
        REQUIRE(events.size() == 1);
        CHECK(std::any_cast<const UserActionResolvedEvent&>(events[0].payload).resolution ==
              RequestResolution::Withdrawn);
    }
}

TEST_CASE("A nested answer while validating is refused", "[foundation][requests][race]") {
    Rig rig;
    RequestId id{};
    std::optional<Result<void>> nested;
    id = rig.requests.ask(
        UserRequestKind::ConfirmStopSessions, {}, std::nullopt, std::nullopt,
        [&](const std::any&) -> Result<void> {
            nested = rig.requests.respond(id, {});
            return {};
        },
        CancelToken{});
    CHECK(rig.requests.respond(id, {}));
    REQUIRE(nested);
    REQUIRE_FALSE(*nested);
    CHECK(nested->error().id == "requests.already_resolved");
}

TEST_CASE("A validator may raise another request", "[foundation][requests]") {
    Rig rig;
    std::optional<RequestId> follow_up;
    const RequestId id = rig.requests.ask(
        UserRequestKind::AccountRenameConflict, {}, std::nullopt, std::nullopt,
        [&](const std::any&) -> Result<void> {
            follow_up = rig.requests.ask(UserRequestKind::NeedsJoinPassword, {}, std::nullopt, std::nullopt,
                                         accept_any, CancelToken{});
            return {};
        },
        CancelToken{});
    CHECK(rig.requests.respond(id, {}));
    REQUIRE(follow_up);
    const auto pending = rig.requests.pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].id == *follow_up);
}

TEST_CASE("An answer prompted by UserActionRequired finds the request", "[foundation][requests]") {
    Rig rig;
    std::optional<Result<void>> answered;
    rig.sub->set_notify([&] {
        if (!answered) answered = rig.requests.respond(RequestId{1}, std::any(std::string("yes")));
    });
    const RequestId id = rig.requests.ask(UserRequestKind::ConfirmJoin, {}, std::nullopt, std::nullopt, want_yes, {});
    CHECK(id == RequestId{1});
    REQUIRE(answered.has_value());
    CHECK(answered->has_value());
    CHECK(rig.requests.pending().empty());

    const std::vector<EventEnvelope> events = rig.drain();
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == EventKind::UserActionRequired);
    CHECK(events[1].kind == EventKind::UserActionResolved);
}
