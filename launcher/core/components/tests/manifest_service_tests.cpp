#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "components_test_support.hpp"
#include "reboot/components/manifest_service.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/trust/serial_guard.hpp"

using namespace rb;
using namespace rb::components;
using rb::components::test::arg_text;
using rb::components::test::bytes_of;
using rb::components::test::ManifestJson;
using rb::components::test::TestSigner;
using rb::components::test::TestStrand;
using rb::testing::FakeHttpResponse;

namespace {

constexpr std::string_view kUrl = "https://cdn.test/manifest.json";
constexpr std::string_view kSigUrl = "https://cdn.test/manifest.json.sig";
const ManifestPlatform kLinux{ManifestOs::Linux, ManifestArch::X64};

// 2026-10-08T00:00:00Z.
constexpr i64 kNowUnixMs = 1791417600000;

std::chrono::system_clock::time_point unix_ms(i64 ms) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

FakeHttpResponse ok_body(std::string_view text) {
    FakeHttpResponse response;
    response.body = bytes_of(text);
    return response;
}

struct Fixture {
    explicit Fixture(ManifestPlatform platform = kLinux, u64 highest_seen = 0)
        : serials(trust::SignedDocumentKind::ReleaseManifest, highest_seen, [this](u64 serial) -> Result<void> {
              if (persist_fault) return std::unexpected(*persist_fault);
              persisted.push_back(serial);
              return {};
          }) {
        clock.set_system(unix_ms(kNowUnixMs));
        ManifestOptions options{std::string(kUrl), std::string(kSigUrl), "stable", platform};
        service = std::make_unique<ManifestService>(
            ManifestServiceDeps{fs, http, workers, strand, clock, keys, serials}, std::move(options), layout, install);
        service->add_listener([this](const ReleaseManifest& manifest) {
            adopted.push_back(manifest.serial);
            cache_at_listener = fs.text(layout.manifest_cache());
        });
    }

    void put_bundled(const std::string& body, bool good_signature = true) {
        fs.write_text(install.bundled_manifest, body);
        fs.write_text(NativePath(install.bundled_manifest) += ".sig",
                      good_signature ? signer.signature_file(body) : other.signature_file(body));
    }

    void put_cache(const std::string& body, bool good_signature = true) {
        fs.write_text(layout.manifest_cache(), body);
        fs.write_text(NativePath(layout.manifest_cache()) += ".sig",
                      good_signature ? signer.signature_file(body) : other.signature_file(body));
    }

    void serve(const std::string& body) {
        transport.route("GET", std::string(kUrl), ok_body(body));
        transport.route("GET", std::string(kSigUrl), ok_body(signer.signature_file(body)));
    }

    Result<ManifestOrigin> load() {
        std::optional<Result<ManifestOrigin>> result;
        service->load([&](Result<ManifestOrigin> loaded) { result = std::move(loaded); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    Result<ManifestRefresh> refresh() {
        std::optional<Result<ManifestRefresh>> result;
        service->refresh({}, [&](Result<ManifestRefresh> refreshed) { result = std::move(refreshed); });
        strand.run_until([&] { return result.has_value(); });
        return std::move(*result);
    }

    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::FakeHttpTransport transport{strand, clock};
    testing::FakeRandom random{3};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, strand, timers, random};
    testing::InMemoryFileSystem fs;
    WorkerPool workers{1};
    TestSigner signer;
    TestSigner other;
    trust::KeyRing keys = signer.ring();
    std::vector<u64> persisted;
    std::optional<Diagnostic> persist_fault;
    trust::SerialGuard serials;
    testing::FakePlatformPaths paths{NativePath("/reboot")};
    AppLayout layout{DataRoot{NativePath("/reboot/data"), true}, paths};
    InstallLayout install{.install_dir = "/app",
                          .backend_exe = "/app/reboot-backend",
                          .game_server_exe = "/app/reboot-game-server",
                          .backend_content_dir = "/app/backend-content",
                          .bundled_catalog = "/app/catalog.json",
                          .bundled_manifest = "/app/manifest.json"};
    std::vector<u64> adopted;
    std::optional<std::string> cache_at_listener;
    std::unique_ptr<ManifestService> service;
};

std::string manifest(u64 serial, u64 expires_unix_ms = test::kFarFutureUnixMs) {
    ManifestJson json;
    json.serial = serial;
    json.expires_unix_ms = expires_unix_ms;
    json.payload("1.0.0", VersionStreams::payload_abi, "client", "winhost");
    return json.text();
}

}  // namespace

TEST_CASE("load falls back to the bundled snapshot without touching the serial floor", "[components][manifest-service]") {
    Fixture f(kLinux, 50);
    f.put_bundled(manifest(7));
    const auto origin = f.load();
    REQUIRE(origin);
    CHECK(*origin == ManifestOrigin::Bundled);
    CHECK(f.service->origin() == ManifestOrigin::Bundled);
    REQUIRE(f.service->current() != nullptr);
    CHECK(f.service->current()->serial == 7);
    CHECK(f.serials.highest_seen() == 50);
    CHECK(f.adopted.empty());
}

TEST_CASE("load prefers a verified cache and admits its serial", "[components][manifest-service]") {
    Fixture f;
    f.put_bundled(manifest(7));
    f.put_cache(manifest(9));
    const auto origin = f.load();
    REQUIRE(origin);
    CHECK(*origin == ManifestOrigin::Cached);
    CHECK(f.service->current()->serial == 9);
    CHECK(f.persisted == std::vector<u64>{9});
}

TEST_CASE("a cache that fails its signature or rolls the serial back is not used", "[components][manifest-service]") {
    SECTION("bad signature") {
        Fixture f;
        f.put_bundled(manifest(7));
        f.put_cache(manifest(9), false);
        CHECK(f.load() == ManifestOrigin::Bundled);
    }
    SECTION("serial below the floor") {
        Fixture f(kLinux, 20);
        f.put_bundled(manifest(7));
        f.put_cache(manifest(9));
        CHECK(f.load() == ManifestOrigin::Bundled);
        CHECK(f.persisted.empty());
    }
}

TEST_CASE("an expired cache stays in use with a warning and never raises the floor", "[components][manifest-service]") {
    Fixture f(kLinux, 3);
    f.put_cache(manifest(9, static_cast<u64>(kNowUnixMs) - 1000));
    CHECK(f.load() == ManifestOrigin::Cached);
    CHECK(f.serials.highest_seen() == 3);
    const auto warning = f.service->expiry_warning();
    REQUIRE(warning);
    CHECK(warning->severity == Severity::Warning);
}

TEST_CASE("load fails with both copies unusable and keeps each reason", "[components][manifest-service]") {
    Fixture f;
    f.put_bundled(manifest(7), false);
    const auto origin = f.load();
    REQUIRE_FALSE(origin);
    CHECK(origin.error().id == "components.manifest_unavailable");
    CHECK(origin.error().causes.size() == 2);
    CHECK(f.service->current() == nullptr);
    CHECK(f.service->payload().error().id == "components.manifest_unavailable");
}

TEST_CASE("refresh adopts a newer manifest, caches it, then runs the listeners", "[components][manifest-service]") {
    Fixture f;
    f.put_bundled(manifest(7));
    REQUIRE(f.load());
    const std::string body = manifest(8);
    f.serve(body);
    const auto refreshed = f.refresh();
    REQUIRE(refreshed);
    CHECK(*refreshed == ManifestRefresh::Adopted);
    CHECK(f.service->origin() == ManifestOrigin::Fetched);
    CHECK(f.service->current()->serial == 8);
    CHECK(f.persisted == std::vector<u64>{8});
    CHECK(f.adopted == std::vector<u64>{8});
    CHECK(f.cache_at_listener == body);
    CHECK(f.fs.text(NativePath(f.layout.manifest_cache()) += ".sig") == f.signer.signature_file(body));

    SECTION("the cached copy is what the next start loads") {
        Fixture next;
        next.put_cache(body);
        CHECK(next.load() == ManifestOrigin::Cached);
        CHECK(next.service->current()->serial == 8);
    }
}

TEST_CASE("refresh of the serial in use is Unchanged", "[components][manifest-service]") {
    Fixture f;
    f.put_cache(manifest(8));
    REQUIRE(f.load());
    f.serve(manifest(8));
    CHECK(f.refresh() == ManifestRefresh::Unchanged);
    CHECK(f.adopted.empty());
    CHECK(f.service->origin() == ManifestOrigin::Cached);
}

TEST_CASE("a refused fetch leaves the copy in use untouched", "[components][manifest-service]") {
    Fixture f;
    f.put_cache(manifest(8));
    REQUIRE(f.load());

    SECTION("lower serial") {
        f.serve(manifest(5));
        const auto refreshed = f.refresh();
        REQUIRE_FALSE(refreshed);
        CHECK(refreshed.error().id.starts_with("trust."));
    }
    SECTION("bad signature") {
        const std::string body = manifest(9);
        f.transport.route("GET", std::string(kUrl), ok_body(body));
        f.transport.route("GET", std::string(kSigUrl), ok_body(f.other.signature_file(body)));
        CHECK_FALSE(f.refresh());
    }
    SECTION("expired, checked before the serial so the floor stays") {
        f.serve(manifest(9, static_cast<u64>(kNowUnixMs) - 1));
        const auto refreshed = f.refresh();
        REQUIRE_FALSE(refreshed);
        CHECK(refreshed.error().id == "components.manifest_expired");
        CHECK(arg_text(refreshed.error(), "expires_at") == "2026-10-07T23:59:59Z");
        CHECK(f.serials.highest_seen() == 8);
    }
    SECTION("malformed body") {
        f.serve(R"({"schema":1,"serial":9})");
        const auto refreshed = f.refresh();
        REQUIRE_FALSE(refreshed);
        CHECK(refreshed.error().id == "components.manifest_malformed");
    }
    SECTION("the serial cannot be persisted") {
        f.persist_fault = make_diag(ErrorDomain::Storage, MessageId{"components_test.persist"}).build();
        f.serve(manifest(9));
        CHECK_FALSE(f.refresh());
        CHECK(f.serials.highest_seen() == 8);
    }
    SECTION("HTTP status") {
        FakeHttpResponse missing;
        missing.status = 404;
        f.transport.route("GET", std::string(kUrl), missing);
        const auto refreshed = f.refresh();
        REQUIRE_FALSE(refreshed);
        CHECK(refreshed.error().id == "components.manifest_fetch_failed");
        CHECK(arg_text(refreshed.error(), "status") == "404");
        CHECK_FALSE(refreshed.error().retryable);
    }
    SECTION("transport failure") {
        FakeHttpResponse broken;
        broken.error = make_diag(ErrorDomain::Net, MessageId{"components_test.dns"}).build();
        f.transport.route("GET", std::string(kUrl), broken);
        const auto refreshed = f.refresh();
        REQUIRE_FALSE(refreshed);
        CHECK(refreshed.error().id == "components.manifest_fetch_failed");
        CHECK(arg_text(refreshed.error(), "url") == kUrl);
    }
    CHECK(f.service->current()->serial == 8);
    CHECK(f.service->origin() == ManifestOrigin::Cached);
    CHECK(f.adopted.empty());
    CHECK(f.fs.text(f.layout.manifest_cache()) == manifest(8));
}

TEST_CASE("a refresh while one is in flight joins it", "[components][manifest-service]") {
    Fixture f;
    f.put_bundled(manifest(7));
    REQUIRE(f.load());
    f.serve(manifest(8));
    std::vector<Result<ManifestRefresh>> results;
    f.service->refresh({}, [&](Result<ManifestRefresh> r) { results.push_back(std::move(r)); });
    f.service->refresh({}, [&](Result<ManifestRefresh> r) { results.push_back(std::move(r)); });
    f.strand.run_until([&] { return results.size() == 2; });
    CHECK(results[0] == ManifestRefresh::Adopted);
    CHECK(results[1] == ManifestRefresh::Adopted);
    CHECK(f.transport.requests().size() == 2);
    CHECK(f.adopted == std::vector<u64>{8});
}

TEST_CASE("cancelling a joined refresh ends only that caller's wait", "[components][manifest-service]") {
    Fixture f;
    f.put_bundled(manifest(7));
    REQUIRE(f.load());
    const std::string body = manifest(8);
    FakeHttpResponse slow = ok_body(body);
    slow.delay = std::chrono::seconds{5};
    f.transport.route("GET", std::string(kUrl), slow);
    f.transport.route("GET", std::string(kSigUrl), ok_body(f.signer.signature_file(body)));

    CancelSource first_cancel;
    std::optional<Result<ManifestRefresh>> first;
    std::optional<Result<ManifestRefresh>> second;
    f.service->refresh(first_cancel.token(), [&](Result<ManifestRefresh> r) { first = std::move(r); });

    SECTION("a joined caller still gets the manifest") {
        f.service->refresh({}, [&](Result<ManifestRefresh> r) { second = std::move(r); });
        first_cancel.cancel(CancelReason::User);
        f.strand.run_until([&] { return first.has_value(); });
        REQUIRE_FALSE(*first);
        CHECK(first->error().kind == ErrorKind::Cancelled);
        f.strand.advance(std::chrono::seconds{5});
        f.strand.run_until([&] { return second.has_value(); });
        CHECK(*second == ManifestRefresh::Adopted);
        CHECK(f.adopted == std::vector<u64>{8});
    }
    SECTION("the last caller's cancel stops the refresh; a later one starts afresh") {
        first_cancel.cancel(CancelReason::User);
        f.strand.run_until([&] { return first.has_value(); });
        CHECK(first->error().kind == ErrorKind::Cancelled);
        f.service->refresh({}, [&](Result<ManifestRefresh> r) { second = std::move(r); });
        f.strand.advance(std::chrono::seconds{5});
        f.strand.advance(std::chrono::seconds{5});
        f.strand.run_until([&] { return second.has_value(); });
        CHECK(*second == ManifestRefresh::Adopted);
        CHECK(f.adopted == std::vector<u64>{8});
    }
}

TEST_CASE("load takes a bundled snapshot newer than the cache", "[components][manifest-service]") {
    Fixture f;
    f.put_cache(manifest(5));
    f.put_bundled(manifest(9));
    CHECK(f.load() == ManifestOrigin::Bundled);
    CHECK(f.service->current()->serial == 9);
    CHECK(f.persisted == std::vector<u64>{5});
}

TEST_CASE("load keeps a manifest a refresh adopted first", "[components][manifest-service]") {
    Fixture f;
    f.put_bundled(manifest(7));
    f.serve(manifest(8));
    REQUIRE(f.refresh() == ManifestRefresh::Adopted);
    CHECK(f.load() == ManifestOrigin::Fetched);
    CHECK(f.service->current()->serial == 8);
}

TEST_CASE("queries select this platform and channel", "[components][manifest-service]") {
    ManifestJson json;
    json.apps.push_back(R"({"platform":{"os":"linux","arch":"x64"},"channel":"stable","version":"1.2.0","kind":"tarball","package":)" +
                        test::remote_json("https://cdn.test/app.tar", "app") + "}");
    json.apps.push_back(R"({"platform":{"os":"linux","arch":"x64"},"channel":"beta","version":"1.3.0","kind":"tarball","package":)" +
                        test::remote_json("https://cdn.test/beta.tar", "beta") + "}");
    json.payload("1.0.0", VersionStreams::payload_abi, "old", "winhost")
        .payload("1.5.0", VersionStreams::payload_abi, "client-only")
        .payload("1.2.0", VersionStreams::payload_abi, "new", "winhost")
        .payload("9.0.0", static_cast<u16>(VersionStreams::payload_abi + 1), "future", "winhost");
    json.runtime("kron", "kron_wine", "linux", "https://cdn.test/kron.tar.xz", "kron")
        .runtime("gcenx", "mac_wine", "macos", "https://cdn.test/gcenx.tar.xz", "gcenx")
        .runtime("vc", "vc_redist", std::nullopt, "https://cdn.test/vc.zip", "vc");
    json.endpoint = R"({"host":"sb.test","port":9000})";

    SECTION("linux") {
        Fixture f;
        f.put_bundled(json.text());
        REQUIRE(f.load());
        const auto app = f.service->app_entry();
        REQUIRE(app);
        CHECK(app->version.to_string() == "1.2.0");
        CHECK(f.service->update_offer(*SemVer::parse("1.1.0")));
        CHECK_FALSE(f.service->update_offer(*SemVer::parse("1.2.0")));
        CHECK(f.service->endpoint_override() == EndpointOverride{"sb.test", Port{9000}});

        // 1.5.0 lacks winhost, which play under Wine needs; 9.0.0 has another ABI.
        const auto payload = f.service->payload();
        REQUIRE(payload);
        CHECK(payload->version.to_string() == "1.2.0");

        CHECK(f.service->runtime("kron"));
        CHECK(f.service->runtime("vc"));
        CHECK(f.service->runtime("gcenx").error().id == "components.runtime_wrong_platform");
        CHECK(f.service->runtime("nope").error().id == "components.unknown_runtime");
        std::vector<std::string> ids;
        for (const RuntimeEntry& runtime : f.service->runtimes()) ids.push_back(runtime.id);
        CHECK(ids == std::vector<std::string>{"kron", "vc"});
    }
    SECTION("windows") {
        Fixture f(ManifestPlatform{ManifestOs::Windows, ManifestArch::X64});
        f.put_bundled(json.text());
        REQUIRE(f.load());
        CHECK_FALSE(f.service->app_entry());
        CHECK(f.service->payload()->version.to_string() == "1.5.0");
        CHECK(f.service->runtimes().empty());
        CHECK(f.service->runtime("vc").error().id == "components.runtime_wrong_platform");
    }
    SECTION("no payload for this ABI") {
        ManifestJson only_future;
        only_future.payload("9.0.0", static_cast<u16>(VersionStreams::payload_abi + 1), "future", "winhost");
        Fixture f;
        f.put_bundled(only_future.text());
        REQUIRE(f.load());
        const auto payload = f.service->payload();
        REQUIRE_FALSE(payload);
        CHECK(payload.error().id == "components.no_payload");
        CHECK(arg_text(payload.error(), "payload_abi") == std::to_string(VersionStreams::payload_abi));
    }
}
