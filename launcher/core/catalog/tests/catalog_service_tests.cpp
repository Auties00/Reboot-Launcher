#include <any>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/catalog/catalog_service.hpp"
#include "reboot/catalog/catalog_source.hpp"
#include "reboot/catalog/catalog_updated.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/event_recorder.hpp"

using namespace reboot;
using namespace reboot::catalog;
using namespace std::chrono_literals;

namespace {

// Holds each load until the test answers it, so every interleaving is chosen by the test.
class ScriptedSource final : public ICatalogSource {
public:
    void load(CatalogFetch fetch, CancelToken token, UniqueFunction<void(CatalogLoadResult)> done) override {
        REQUIRE_FALSE(pending_.has_value());
        fetches.push_back(fetch);
        pending_.emplace(Pending{fetch, std::move(token), std::move(done)});
    }

    [[nodiscard]] bool pending() const noexcept { return pending_.has_value(); }
    [[nodiscard]] CatalogFetch pending_fetch() const { return pending_->fetch; }
    [[nodiscard]] bool pending_cancelled() const { return pending_->token.cancelled(); }

    void answer(CatalogLoadResult result) {
        REQUIRE(pending_.has_value());
        auto done = std::move(pending_->done);
        pending_.reset();
        done(std::move(result));
    }

    std::vector<CatalogFetch> fetches;

private:
    struct Pending {
        CatalogFetch fetch;
        CancelToken token;
        UniqueFunction<void(CatalogLoadResult)> done;
    };
    std::optional<Pending> pending_;
};

GameVersion version(std::string_view text) {
    auto parsed = GameVersion::parse(text);
    REQUIRE(parsed);
    return *parsed;
}

constexpr auto kExpiry = std::chrono::system_clock::time_point(std::chrono::hours{24});

LoadedCatalog loaded(u64 serial, CatalogOrigin origin, std::chrono::system_clock::time_point expires_at = kExpiry) {
    Catalog catalog;
    catalog.schema = kCatalogSchema;
    catalog.serial = serial;
    catalog.expires_at = expires_at;

    CatalogEntry cert;
    cert.id = "cert";
    cert.version = version("3.5");
    cert.aliases = {"3.50.1"};
    cert.format = ArchiveFormat::Zip;
    cert.container = ArchiveContainer::None;
    cert.availability = Availability::Withdrawn;

    CatalogEntry latest;
    latest.id = "12.41";
    latest.version = version("12.41");
    latest.display_name = "Fortnite 12.41";
    latest.format = ArchiveFormat::SevenZip;
    latest.container = ArchiveContainer::ZipStored;
    latest.availability = Availability::Available;

    catalog.entries = {cert, latest};
    catalog.flag_ranges = {BuildFlagRange{VersionRange{version("1.0"), version("30.10")}, BuildFlags{}}};
    return LoadedCatalog{.catalog = std::move(catalog), .origin = origin, .warnings = {}};
}

CatalogLoadResult failure(CatalogErrorCode code) { return std::unexpected(CatalogError{.code = code}); }

struct Fixture {
    testing::DeterministicRuntime runtime;
    testing::EventRecorder recorder{runtime.events(), EventFilter{.kinds = {EventKind::CatalogChanged}}};
    ScriptedSource remote;
    ScriptedSource bundled;
    CatalogService service{remote, bundled, runtime.ops(), runtime.events(), runtime.clock()};

    OpHandle refresh(CatalogRefresh mode = CatalogRefresh::Force) {
        auto handle = service.start_refresh(mode, DisconnectPolicy::Detached);
        REQUIRE(handle);
        return *handle;
    }

    // Answers the startup pair: the remote's cache, then the bundled copy.
    void answer_local(CatalogLoadResult cached, CatalogLoadResult shipped) {
        REQUIRE(remote.pending());
        CHECK(remote.pending_fetch() == CatalogFetch::CacheOnly);
        remote.answer(std::move(cached));
        REQUIRE(bundled.pending());
        CHECK(bundled.pending_fetch() == CatalogFetch::CacheOnly);
        bundled.answer(std::move(shipped));
    }

    void answer_remote(CatalogLoadResult fetched) {
        REQUIRE(remote.pending());
        CHECK(remote.pending_fetch() == CatalogFetch::Revalidate);
        remote.answer(std::move(fetched));
    }

    [[nodiscard]] std::optional<ErasedOutcome> outcome(OpHandle handle) { return runtime.ops().outcome(handle.id()); }

    CatalogUpdated completed(OpHandle handle) {
        const auto result = outcome(handle);
        REQUIRE(result);
        REQUIRE(std::holds_alternative<Completed<std::any>>(*result));
        return std::any_cast<CatalogUpdated>(std::get<Completed<std::any>>(*result).value);
    }

    std::vector<CatalogUpdated> changes() {
        recorder.pump();
        std::vector<CatalogUpdated> out;
        for (const CatalogUpdated* change : recorder.payloads<CatalogUpdated>(EventKind::CatalogChanged))
            out.push_back(*change);
        recorder.clear();
        return out;
    }

    // Starts with `serial` active from the cache and the remote revalidated to the same copy.
    void start_with(u64 serial) {
        const OpHandle handle = refresh();
        answer_local(loaded(serial, CatalogOrigin::Cache), failure(CatalogErrorCode::BundledUnusable));
        answer_remote(loaded(serial, CatalogOrigin::Cache));
        REQUIRE(outcome(handle));
        (void)changes();
    }
};

bool has_warning(const std::vector<Diagnostic>& warnings, std::string_view id) {
    for (const Diagnostic& warning : warnings)
        if (warning.id == id) return warning.severity == Severity::Warning;
    return false;
}

}  // namespace

TEST_CASE("nothing is active before the first refresh", "[catalog][service]") {
    Fixture f;
    CHECK(f.service.current().entries.empty());
    CHECK_FALSE(f.service.origin());
    CHECK(f.service.list(CatalogFilter{.include_unavailable = true}).empty());
    const auto missing = f.service.entry("12.41");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "catalog.entry_not_found");
}

TEST_CASE("startup activates the higher local serial, then the remote copy", "[catalog][service]") {
    Fixture f;
    const OpHandle handle = f.refresh(CatalogRefresh::Force);
    f.answer_local(loaded(5, CatalogOrigin::Cache), loaded(7, CatalogOrigin::Bundled));
    CHECK(f.service.current().serial == 7);
    CHECK(f.service.origin() == CatalogOrigin::Bundled);
    CHECK_FALSE(f.outcome(handle));

    f.answer_remote(loaded(9, CatalogOrigin::Remote));
    CHECK(f.service.current().serial == 9);
    CHECK(f.service.origin() == CatalogOrigin::Remote);

    const CatalogUpdated result = f.completed(handle);
    CHECK(result.serial == 9);
    CHECK(result.origin == CatalogOrigin::Remote);
    CHECK(result.entries == 2);
    CHECK(result.installable == 1);
    CHECK(result.warnings.empty());

    const auto changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].serial == 9);
    CHECK(f.remote.fetches == std::vector{CatalogFetch::CacheOnly, CatalogFetch::Revalidate});
    CHECK(f.bundled.fetches == std::vector{CatalogFetch::CacheOnly});
}

TEST_CASE("on a serial tie the cache wins over the bundled copy", "[catalog][service]") {
    Fixture f;
    const OpHandle handle = f.refresh(CatalogRefresh::IfExpired);
    f.answer_local(loaded(5, CatalogOrigin::Cache), loaded(5, CatalogOrigin::Bundled));
    CHECK(f.service.origin() == CatalogOrigin::Cache);
    CHECK(f.completed(handle).origin == CatalogOrigin::Cache);
}

TEST_CASE("IfExpired skips the network while the active copy is fresh", "[catalog][service]") {
    Fixture f;
    const OpHandle handle = f.refresh(CatalogRefresh::IfExpired);
    // A first run has no cache, which needs no warning.
    f.answer_local(failure(CatalogErrorCode::CacheMissing), loaded(3, CatalogOrigin::Bundled));
    CHECK_FALSE(f.remote.pending());
    const CatalogUpdated result = f.completed(handle);
    CHECK(result.origin == CatalogOrigin::Bundled);
    CHECK(result.warnings.empty());
    CHECK(f.remote.fetches == std::vector{CatalogFetch::CacheOnly});

    const OpHandle again = f.refresh(CatalogRefresh::IfExpired);
    CHECK_FALSE(f.remote.pending());
    CHECK(f.completed(again).serial == 3);
    CHECK(f.changes().size() == 1);
}

TEST_CASE("a cache that exists but cannot be read is a warning", "[catalog][service]") {
    const auto unreadable = [](ErrorKind kind) -> CatalogLoadResult {
        Diagnostic cause = make_diag(ErrorDomain::Storage, MessageId{"storage.test_read"}).kind(kind).build();
        return std::unexpected(CatalogError{.code = CatalogErrorCode::CacheMissing, .cause = std::move(cause)});
    };
    Fixture f;
    const OpHandle denied = f.refresh(CatalogRefresh::IfExpired);
    f.answer_local(unreadable(ErrorKind::Generic), loaded(3, CatalogOrigin::Bundled));
    CHECK(has_warning(f.completed(denied).warnings, "catalog.cache_missing"));

    Fixture first_run;
    const OpHandle absent = first_run.refresh(CatalogRefresh::IfExpired);
    first_run.answer_local(unreadable(ErrorKind::NotFound), loaded(3, CatalogOrigin::Bundled));
    CHECK(first_run.completed(absent).warnings.empty());
}

TEST_CASE("an expired copy is revalidated and stays in use with warnings when that fails", "[catalog][service]") {
    Fixture f;
    f.runtime.clock().set_system(kExpiry + 1h);
    const OpHandle handle = f.refresh(CatalogRefresh::IfExpired);
    f.answer_local(loaded(5, CatalogOrigin::Cache), failure(CatalogErrorCode::BundledUnusable));
    f.answer_remote(std::unexpected(CatalogError{.code = CatalogErrorCode::FetchFailed, .url = "https://catalog.test"}));

    const CatalogUpdated result = f.completed(handle);
    CHECK(result.serial == 5);
    CHECK(result.origin == CatalogOrigin::Cache);
    CHECK(has_warning(result.warnings, "catalog.bundled_unusable"));
    CHECK(has_warning(result.warnings, "catalog.fetch_failed"));
    CHECK(has_warning(result.warnings, "trust.document_expired"));

    const auto changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(has_warning(changes[0].warnings, "trust.document_expired"));
}

TEST_CASE("the op fails only when no copy is usable", "[catalog][service]") {
    Fixture f;
    const OpHandle handle = f.refresh(CatalogRefresh::IfExpired);
    f.answer_local(failure(CatalogErrorCode::Untrusted), failure(CatalogErrorCode::BundledUnusable));
    f.answer_remote(std::unexpected(CatalogError{.code = CatalogErrorCode::HttpStatus, .http_status = 503}));

    const auto result = f.outcome(handle);
    REQUIRE(result);
    REQUIRE(std::holds_alternative<Failed>(*result));
    const Diagnostic& error = std::get<Failed>(*result).error;
    CHECK(error.id == "catalog.http_status");
    REQUIRE(error.causes.size() == 2);
    CHECK(error.causes[0].id == "catalog.untrusted");
    CHECK(error.causes[1].id == "catalog.bundled_unusable");
    CHECK_FALSE(f.service.origin());
    CHECK(f.changes().empty());
}

TEST_CASE("a remote failure or unknown schema keeps the active copy as a warning", "[catalog][service]") {
    Fixture f;
    f.start_with(8);

    const OpHandle handle = f.refresh();
    f.answer_remote(std::unexpected(CatalogError{.code = CatalogErrorCode::UnknownSchema, .schema = 2}));
    const CatalogUpdated result = f.completed(handle);
    CHECK(result.serial == 8);
    CHECK(has_warning(result.warnings, "catalog.unknown_schema"));
    CHECK(f.changes().empty());
}

TEST_CASE("a lower serial is never activated, and from the remote it is a rollback warning", "[catalog][service]") {
    Fixture f;
    f.start_with(8);

    const OpHandle rolled_back = f.refresh();
    f.answer_remote(loaded(6, CatalogOrigin::Remote));
    CHECK(f.service.current().serial == 8);
    const CatalogUpdated result = f.completed(rolled_back);
    REQUIRE(has_warning(result.warnings, "catalog.untrusted"));
    CHECK(result.warnings[0].causes[0].id == "trust.serial_rollback");

    const OpHandle older_cache = f.refresh();
    f.answer_remote(loaded(6, CatalogOrigin::Cache));
    CHECK(f.service.current().serial == 8);
    CHECK(f.completed(older_cache).warnings.empty());
    CHECK(f.changes().empty());
}

TEST_CASE("the same serial from another origin is not a change", "[catalog][service]") {
    Fixture f;
    f.start_with(8);
    const OpHandle handle = f.refresh();
    f.answer_remote(loaded(8, CatalogOrigin::Remote));
    CHECK(f.service.origin() == CatalogOrigin::Cache);
    CHECK(f.completed(handle).serial == 8);
    CHECK(f.changes().empty());
}

TEST_CASE("source warnings reach the op result", "[catalog][service]") {
    Fixture f;
    f.start_with(8);
    LoadedCatalog fetched = loaded(9, CatalogOrigin::Remote);
    Diagnostic write_failed = to_diagnostic(CatalogError{.code = CatalogErrorCode::CacheWriteFailed});
    write_failed.severity = Severity::Warning;
    fetched.warnings.push_back(write_failed);

    const OpHandle handle = f.refresh();
    f.answer_remote(std::move(fetched));
    CHECK(has_warning(f.completed(handle).warnings, "catalog.cache_write_failed"));
    CHECK(f.changes().size() == 1);
}

TEST_CASE("a refresh while one runs returns the running op", "[catalog][service]") {
    Fixture f;
    const OpHandle first = f.refresh(CatalogRefresh::IfExpired);
    const OpHandle second = f.refresh(CatalogRefresh::Force);
    CHECK(second.id() == first.id());
    f.answer_local(loaded(5, CatalogOrigin::Cache), loaded(4, CatalogOrigin::Bundled));
    CHECK(f.completed(first).serial == 5);
    CHECK_FALSE(f.remote.pending());
}

TEST_CASE("a refresh after the running op was cancelled queues behind its loads", "[catalog][service]") {
    Fixture f;
    const OpHandle first = f.refresh();
    REQUIRE(f.runtime.ops().cancel(first.id(), CancelReason::User));
    CHECK(f.remote.pending_cancelled());

    const OpHandle second = f.refresh(CatalogRefresh::Force);
    CHECK(second.id() != first.id());
    CHECK(f.refresh().id() == second.id());

    // The cancelled op still activates what the local copies give, then skips the network.
    f.answer_local(loaded(5, CatalogOrigin::Cache), failure(CatalogErrorCode::BundledUnusable));
    CHECK(f.service.current().serial == 5);
    const auto cancelled = f.outcome(first);
    REQUIRE(cancelled);
    CHECK(std::holds_alternative<Cancelled>(*cancelled));
    CHECK(f.changes().size() == 1);

    // The queued op starts now, with a copy active, so it goes straight to the remote.
    f.answer_remote(loaded(6, CatalogOrigin::Remote));
    CHECK(f.completed(second).serial == 6);
    CHECK(f.remote.fetches == std::vector{CatalogFetch::CacheOnly, CatalogFetch::Revalidate});
    const auto changes = f.changes();
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].serial == 6);
}

TEST_CASE("a queued op cancelled before it starts never loads", "[catalog][service]") {
    Fixture f;
    const OpHandle first = f.refresh();
    REQUIRE(f.runtime.ops().cancel(first.id(), CancelReason::User));
    const OpHandle queued = f.refresh();
    REQUIRE(f.runtime.ops().cancel(queued.id(), CancelReason::User));

    // A call while the cancelled one waits replaces it.
    const OpHandle replacement = f.refresh();
    CHECK(replacement.id() != queued.id());
    REQUIRE(f.runtime.ops().cancel(replacement.id(), CancelReason::User));

    f.answer_local(loaded(5, CatalogOrigin::Cache), loaded(4, CatalogOrigin::Bundled));
    CHECK_FALSE(f.remote.pending());
    CHECK_FALSE(f.bundled.pending());
    CHECK(std::holds_alternative<Cancelled>(*f.outcome(replacement)));

    const OpHandle later = f.refresh(CatalogRefresh::IfExpired);
    CHECK(f.completed(later).serial == 5);
}

TEST_CASE("a refresh that hits its deadline ends without the network", "[catalog][service]") {
    Fixture f;
    const OpHandle handle = f.refresh();
    f.runtime.advance(default_deadline(OpKind::HttpSmall) + 1s);
    const auto result = f.outcome(handle);
    REQUIRE(result);
    CHECK(std::holds_alternative<TimedOut>(*result));

    f.answer_local(loaded(5, CatalogOrigin::Cache), loaded(4, CatalogOrigin::Bundled));
    CHECK_FALSE(f.remote.pending());
    CHECK(f.service.current().serial == 5);

    const OpHandle next = f.refresh();
    CHECK(next.id() != handle.id());
    f.answer_remote(loaded(5, CatalogOrigin::Cache));
    CHECK(f.completed(next).serial == 5);
}

TEST_CASE("destroying the service ends its ops and ignores loads that finish later", "[catalog][service]") {
    testing::DeterministicRuntime runtime;
    ScriptedSource remote;
    ScriptedSource bundled;
    std::optional<CatalogService> service;
    service.emplace(remote, bundled, runtime.ops(), runtime.events(), runtime.clock());

    const auto first = service->start_refresh(CatalogRefresh::Force, DisconnectPolicy::Detached);
    REQUIRE(first);
    SECTION("running") {
        service.reset();
        const auto outcome = runtime.ops().outcome(first->id());
        REQUIRE(outcome);
        REQUIRE(std::holds_alternative<Cancelled>(*outcome));
        CHECK(std::get<Cancelled>(*outcome).reason == CancelReason::Shutdown);
        CHECK(remote.pending_cancelled());
    }
    SECTION("queued") {
        REQUIRE(runtime.ops().cancel(first->id(), CancelReason::User));
        const auto queued = service->start_refresh(CatalogRefresh::Force, DisconnectPolicy::Detached);
        REQUIRE(queued);
        service.reset();
        const auto outcome = runtime.ops().outcome(queued->id());
        REQUIRE(outcome);
        REQUIRE(std::holds_alternative<Cancelled>(*outcome));
        CHECK(std::get<Cancelled>(*outcome).reason == CancelReason::Shutdown);
    }
    remote.answer(loaded(5, CatalogOrigin::Cache));
    CHECK_FALSE(bundled.pending());
}

TEST_CASE("entry takes an id or an alias, and installable_entry refuses what cannot install", "[catalog][service]") {
    Fixture f;
    f.start_with(8);

    const auto by_alias = f.service.entry(" 3.50.1 ");
    REQUIRE(by_alias);
    CHECK(by_alias->id == "cert");
    CHECK(f.service.entry("CERT")->id == "cert");
    CHECK(f.service.installable_entry("12.41")->display_name == "Fortnite 12.41");

    const auto withdrawn = f.service.installable_entry("cert");
    REQUIRE_FALSE(withdrawn);
    CHECK(withdrawn.error().id == "catalog.entry_not_installable");
    CHECK(withdrawn.error().kind == ErrorKind::Conflict);

    const auto unknown = f.service.installable_entry("9.99");
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().id == "catalog.entry_not_found");
    CHECK(unknown.error().kind == ErrorKind::NotFound);
}

TEST_CASE("list keeps catalog order and filters what cannot install", "[catalog][service]") {
    Fixture f;
    f.start_with(8);
    const auto installable = f.service.list(CatalogFilter{});
    REQUIRE(installable.size() == 1);
    CHECK(installable[0].id == "12.41");

    const auto all = f.service.list(CatalogFilter{.include_unavailable = true});
    REQUIRE(all.size() == 2);
    CHECK(all[0].id == "cert");
    CHECK(all[1].id == "12.41");
    CHECK(f.service.aliases().resolve("3.50.1")->entry == "cert");
}
