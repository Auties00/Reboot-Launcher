#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/logging/error_router.hpp"

using namespace rb;
using namespace rb::logging;

namespace {

struct RouterFixture {
    RouterFixture() : strand(clock), router(strand, clock) {
        static const bool installed = [] {
            Logger::install(1u << 20);
            return true;
        }();
        (void)installed;
        router.set_on_change([this](const BackgroundFailureChange& change) { changes.push_back(change); });
    }

    ManualClock clock;
    ManualExecutor strand;
    ErrorRouter router;
    std::vector<BackgroundFailureChange> changes;
};

Diagnostic diag_of(std::string id, std::string path, std::optional<std::string> detail = std::nullopt) {
    Diagnostic diag;
    diag.id = std::move(id);
    diag.args = {{"path", Arg{std::move(path)}}};
    diag.detail = std::move(detail);
    return diag;
}

SessionId session_of(u8 fill) {
    SessionId session;
    session.value.bytes.fill(fill);
    return session;
}

}  // namespace

TEST_CASE("a repeat counts on the unacknowledged entry and keeps the newest diagnostic", "[logging][errors]") {
    RouterFixture f;
    f.router.report(diag_of("storage.read_failed", "a", "first"), LogCategory::Storage, std::nullopt);
    f.clock.advance(std::chrono::hours{3});
    f.router.report(diag_of("storage.read_failed", "a", "second"), LogCategory::Storage, std::nullopt);

    const auto kept = f.router.background_failures();
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].occurrences == 2);
    CHECK(kept[0].diag.detail == std::optional<std::string>("second"));
    CHECK(kept[0].last_at - kept[0].first_at == std::chrono::hours{3});
    REQUIRE(f.changes.size() == 2);
    CHECK(f.changes[1].failure->occurrences == 2);
}

TEST_CASE("different args, category or session are different failures", "[logging][errors]") {
    RouterFixture f;
    f.router.report(diag_of("storage.read_failed", "a"), LogCategory::Storage, std::nullopt);
    f.router.report(diag_of("storage.read_failed", "b"), LogCategory::Storage, std::nullopt);
    f.router.report(diag_of("storage.read_failed", "a"), LogCategory::Engine, std::nullopt);
    f.router.report(diag_of("storage.read_failed", "a"), LogCategory::Storage, session_of(1));
    CHECK(f.router.background_failures().size() == 4);
}

TEST_CASE("an acknowledged failure shows again when it repeats", "[logging][errors]") {
    RouterFixture f;
    f.router.report(diag_of("storage.read_failed", "a"), LogCategory::Storage, std::nullopt);
    const BackgroundFailureId first = f.router.background_failures().at(0).id;
    f.router.acknowledge(first);
    f.router.acknowledge(first);
    CHECK(f.router.background_failures().empty());
    REQUIRE(f.changes.size() == 2);
    CHECK_FALSE(f.changes[1].failure.has_value());

    f.router.report(diag_of("storage.read_failed", "a"), LogCategory::Storage, std::nullopt);
    const auto kept = f.router.background_failures();
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].id != first);
    CHECK(kept[0].occurrences == 1);
}

TEST_CASE("past the limit the oldest failure is dropped and reported gone", "[logging][errors]") {
    RouterFixture f;
    for (std::size_t i = 0; i <= kMaxBackgroundFailures; ++i)
        f.router.report(diag_of("storage.read_failed", std::to_string(i)), LogCategory::Storage, std::nullopt);
    const auto kept = f.router.background_failures();
    REQUIRE(kept.size() == kMaxBackgroundFailures);
    CHECK(kept.front().diag.find_arg("path") != nullptr);
    CHECK(std::get<std::string>(*kept.front().diag.find_arg("path")) == "1");
    bool dropped_reported = false;
    for (const BackgroundFailureChange& change : f.changes)
        if (change.id == BackgroundFailureId{1} && !change.failure) dropped_reported = true;
    CHECK(dropped_reported);
}

TEST_CASE("a late subscriber gets every kept failure replayed", "[logging][errors]") {
    RouterFixture f;
    f.router.report(diag_of("storage.read_failed", "a"), LogCategory::Storage, std::nullopt);
    f.router.report(diag_of("storage.read_failed", "b"), LogCategory::Storage, std::nullopt);
    std::vector<BackgroundFailureChange> replayed;
    f.router.set_on_change([&replayed](const BackgroundFailureChange& change) { replayed.push_back(change); });
    CHECK(replayed.size() == 2);
}

TEST_CASE("op failures get a log_ref before they are published", "[logging][errors]") {
    RouterFixture f;
    ErasedOutcome failed = Failed{diag_of("play.launch_failed", "x")};
    f.router.record_outcome(OpId{7}, OpKind::Play, std::nullopt, failed);
    ErasedOutcome again = Failed{diag_of("play.launch_failed", "x")};
    f.router.record_outcome(OpId{8}, OpKind::Play, std::nullopt, again);

    const auto& first_ref = std::get<Failed>(failed).error.log_ref;
    const auto& second_ref = std::get<Failed>(again).error.log_ref;
    REQUIRE(first_ref);
    REQUIRE(second_ref);
    CHECK(second_ref->seq > first_ref->seq);
    CHECK(f.router.background_failures().empty());

    ErasedOutcome cancelled = Cancelled{CancelReason{}};
    f.router.record_outcome(OpId{9}, OpKind::Play, std::nullopt, cancelled);
    CHECK(std::holds_alternative<Cancelled>(cancelled));
}
