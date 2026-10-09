#include <any>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/reset_service.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/storage/settings_document.hpp"
#include "reboot/storage/settings_registry.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "test_support.hpp"

using namespace reboot;
using namespace reboot::storage;

namespace {

struct Fixture {
    Fixture() {
        static_cast<void>(store.load_memory_only(make_diag(ErrorDomain::Storage, MessageId{"storage.memory_only"})));
        SettingsPatch listed;
        listed.host.listing = HostListing::Listed;
        REQUIRE(settings.patch(listed));
    }

    [[nodiscard]] ResetHooks hooks() {
        return ResetHooks{
            .find_blockers = [this](ResetGroup) { return blockers; },
            .stop_blockers =
                [this](ResetGroup, ResetBlockers stopping, CancelToken token,
                       UniqueFunction<void(Result<void>)> done) {
                    stopped = std::move(stopping);
                    stop_token = std::move(token);
                    stop_done = std::move(done);
                },
            .reset_records =
                [this](ResetGroup group) -> Result<void> {
                    records_reset.push_back(group);
                    return {};
                },
        };
    }

    test::WorkerStrand strand;
    WorkerPool workers{1};
    testing::DeterministicRuntime runtime;
    testing::InMemoryFileSystem fs;
    DocumentStore<SettingsDocument> store{fs, workers, strand, runtime.clock(),
                                          testing::default_fake_root() / "settings.json"};
    SettingsRegistry registry;
    Settings settings{store, registry, runtime.events()};

    ResetBlockers blockers;
    std::optional<ResetBlockers> stopped;
    CancelToken stop_token;
    UniqueFunction<void(Result<void>)> stop_done;
    std::vector<ResetGroup> records_reset;
    ResetService service{settings, registry, runtime.ops(), hooks()};
};

}  // namespace

TEST_CASE("a reset refuses while sessions still run", "[storage][reset]") {
    Fixture f;
    f.blockers.sessions.push_back(SessionId{});
    const Result<ResetReport> refused = f.service.reset(ResetGroup::Host);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "storage.reset_blocked");
    CHECK(f.records_reset.empty());
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Listed);
}

TEST_CASE("a Host reset resets host profiles and returns the listing to Unlisted", "[storage][reset]") {
    Fixture f;
    const Result<ResetReport> report = f.service.reset(ResetGroup::Host);
    REQUIRE(report);
    CHECK(report->keys == std::vector<std::string>{"host.update_policy", "host.listing"});
    CHECK(f.records_reset == std::vector<ResetGroup>{ResetGroup::Host});
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Unlisted);
}

TEST_CASE("a reset after stop is an op that completes once the blockers stopped", "[storage][reset]") {
    Fixture f;
    f.blockers.backend_running = true;
    const Result<OpHandle> handle = f.service.start_reset_after_stop(ResetGroup::Backend, DisconnectPolicy::Detached);
    REQUIRE(handle);
    REQUIRE(f.stopped);
    CHECK(f.stopped->backend_running);
    CHECK_FALSE(f.runtime.ops().outcome(handle->id()).has_value());

    f.blockers = {};
    f.stop_done({});
    const std::optional<ErasedOutcome> outcome = f.runtime.ops().outcome(handle->id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<Completed<std::any>>(*outcome));
    CHECK(f.records_reset == std::vector<ResetGroup>{ResetGroup::Backend});
}

TEST_CASE("a failed stop fails the op and resets nothing", "[storage][reset]") {
    Fixture f;
    f.blockers.sessions.push_back(SessionId{});
    const Result<OpHandle> handle = f.service.start_reset_after_stop(ResetGroup::Host, DisconnectPolicy::Detached);
    REQUIRE(handle);
    f.stop_done(make_diag(ErrorDomain::Storage, MessageId{"storage.cancelled"}).fail());

    const std::optional<ErasedOutcome> outcome = f.runtime.ops().outcome(handle->id());
    REQUIRE(outcome);
    const Failed* failed = std::get_if<Failed>(&*outcome);
    REQUIRE(failed != nullptr);
    CHECK(failed->error.id == "storage.reset_stop_failed");
    CHECK(f.records_reset.empty());
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Listed);
}

TEST_CASE("a reset after stop with nothing running completes at once", "[storage][reset]") {
    Fixture f;
    const Result<OpHandle> handle = f.service.start_reset_after_stop(ResetGroup::Host, DisconnectPolicy::Detached);
    REQUIRE(handle);
    CHECK_FALSE(f.stopped);
    const std::optional<ErasedOutcome> outcome = f.runtime.ops().outcome(handle->id());
    REQUIRE(outcome);
    const auto* completed = std::get_if<Completed<std::any>>(&*outcome);
    REQUIRE(completed != nullptr);
    CHECK(std::any_cast<ResetReport>(completed->value).keys ==
          std::vector<std::string>{"host.update_policy", "host.listing"});
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Unlisted);
}

TEST_CASE("blockers that start again while the rest stop fail the reset", "[storage][reset]") {
    Fixture f;
    f.blockers.backend_running = true;
    const Result<OpHandle> handle = f.service.start_reset_after_stop(ResetGroup::Host, DisconnectPolicy::Detached);
    REQUIRE(handle);
    f.stop_done({});

    const std::optional<ErasedOutcome> outcome = f.runtime.ops().outcome(handle->id());
    REQUIRE(outcome);
    const Failed* failed = std::get_if<Failed>(&*outcome);
    REQUIRE(failed != nullptr);
    CHECK(failed->error.id == "storage.reset_blocked");
    CHECK(f.records_reset.empty());
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Listed);
}

TEST_CASE("a reset whose op timed out while stopping resets nothing", "[storage][reset]") {
    Fixture f;
    f.blockers.sessions.push_back(SessionId{});
    const Result<OpHandle> handle = f.service.start_reset_after_stop(ResetGroup::Host, DisconnectPolicy::Detached);
    REQUIRE(handle);
    f.runtime.advance(default_deadline(OpKind::Generic) + std::chrono::seconds{1});
    CHECK(f.stop_token.cancelled());
    const std::optional<ErasedOutcome> timed_out = f.runtime.ops().outcome(handle->id());
    REQUIRE(timed_out);
    CHECK(std::holds_alternative<TimedOut>(*timed_out));

    // The stop finishes anyway, after the op already ended.
    f.blockers = {};
    f.stop_done({});
    CHECK(f.records_reset.empty());
    CHECK(f.settings.snapshot().values.host.listing == HostListing::Listed);
    const std::optional<ErasedOutcome> outcome = f.runtime.ops().outcome(handle->id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<TimedOut>(*outcome));
}
