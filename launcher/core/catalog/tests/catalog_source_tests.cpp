#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "catalog_test_support.hpp"
#include "reboot/catalog/bundled_catalog_source.hpp"
#include "reboot/catalog/catalog_source.hpp"
#include "reboot/catalog/signed_remote_catalog_source.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/trust/key_ring.hpp"
#include "reboot/trust/serial_guard.hpp"

using namespace rb;
using namespace rb::catalog;
using namespace std::chrono_literals;
using rb::catalog::test::bytes_of;
using rb::catalog::test::catalog_json;
using rb::testing::FakeHttpResponse;

namespace {

constexpr const char* kCatalogUrl = "https://catalog.test/catalog.json";
constexpr const char* kSignatureUrl = "https://catalog.test/catalog.json.sig";

FakeHttpResponse answer(u32 status, const std::string& body = {}, std::vector<ports::HttpHeader> headers = {}) {
    FakeHttpResponse response;
    response.status = status;
    response.body = bytes_of(body);
    response.headers = std::move(headers);
    return response;
}

const std::string* header_of(const ports::HttpRequest& request, std::string_view name) {
    for (const ports::HttpHeader& header : request.headers)
        if (header.name == name) return &header.value;
    return nullptr;
}

struct Fixture {
    explicit Fixture(u64 highest_seen = 0)
        : serials(trust::SignedDocumentKind::BuildCatalog, highest_seen, [this](u64 serial) -> Result<void> {
              persisted.push_back(serial);
              return {};
          }) {
        source.emplace(RemoteCatalogLocation{kCatalogUrl, kSignatureUrl}, layout, http, fs, workers, strand, keys,
                       serials);
        bundled.emplace(install, fs, workers, strand, keys);
    }

    // Writes a signed copy into the cache, as an earlier 200 would have.
    void cache(const std::string& body, const std::string& etag = "\"v1\"") {
        fs.write_text(cache_body, body);
        fs.write_text(cache_body.string() + ".sig", signer.signature_file(body));
        fs.write_text(cache_body.string() + ".etag", etag);
    }

    void serve(const std::string& body, const std::string& etag = "\"v2\"") {
        transport.route("GET", kCatalogUrl, answer(200, body, {{"ETag", etag}}));
        transport.route("GET", kSignatureUrl, answer(200, signer.signature_file(body)));
    }

    CatalogLoadResult load(ICatalogSource& target, CatalogFetch fetch, CancelToken token = {}) {
        std::optional<CatalogLoadResult> out;
        int calls = 0;
        target.load(fetch, std::move(token), [&](CatalogLoadResult result) {
            ++calls;
            out = std::move(result);
        });
        strand.run_until([&] { return out.has_value(); });
        strand.run_ready();
        CHECK(calls == 1);
        return std::move(*out);
    }

    CatalogLoadResult remote(CatalogFetch fetch, CancelToken token = {}) { return load(*source, fetch, std::move(token)); }

    ManualClock clock;
    test::TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::FakeHttpTransport transport{strand, clock};
    testing::FakeRandom random{5};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, strand, timers, random};
    testing::InMemoryFileSystem fs;
    testing::FakePlatformPaths paths;
    AppLayout layout{DataRoot{paths.default_data_root(), false}, paths};
    InstallLayout install{.bundled_catalog = paths.exe_dir() / "game-builds.catalog"};
    NativePath cache_body = layout.catalog_cache();
    test::TestSigner signer;
    trust::KeyRing keys = signer.ring();
    std::vector<u64> persisted;
    trust::SerialGuard serials;
    std::optional<SignedRemoteCatalogSource> source;
    std::optional<BundledCatalogSource> bundled;
    // Declared last, so it joins its jobs before what they use goes away.
    WorkerPool workers{1};
};

}  // namespace

TEST_CASE("a cache-only load without a cache is CacheMissing and stays offline", "[catalog][remote]") {
    Fixture f;
    const auto result = f.remote(CatalogFetch::CacheOnly);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == CatalogErrorCode::CacheMissing);
    CHECK(result.error().path == f.cache_body);
    CHECK(f.transport.requests().empty());
}

TEST_CASE("a cache-only load re-verifies the cached copy", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7));
    const auto result = f.remote(CatalogFetch::CacheOnly);
    REQUIRE(result);
    CHECK(result->origin == CatalogOrigin::Cache);
    CHECK(result->catalog.serial == 7);
    CHECK(result->warnings.empty());
    CHECK(f.serials.highest_seen() == 7);
    CHECK(f.transport.requests().empty());
}

TEST_CASE("a cached copy whose signature fails is Untrusted", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7));
    f.fs.write_text(f.cache_body, catalog_json(8));
    const auto result = f.remote(CatalogFetch::CacheOnly);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == CatalogErrorCode::Untrusted);
    REQUIRE(result.error().cause);
    CHECK(result.error().cause->id == "trust.signature_invalid");
}

TEST_CASE("a cached copy below the highest serial seen is a rollback", "[catalog][remote]") {
    Fixture f(9);
    f.cache(catalog_json(7));
    const auto result = f.remote(CatalogFetch::CacheOnly);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == CatalogErrorCode::Untrusted);
    REQUIRE(result.error().cause);
    CHECK(result.error().cause->id == "trust.serial_rollback");
}

TEST_CASE("a 200 is verified, admitted and cached with its signature and ETag", "[catalog][remote]") {
    Fixture f;
    const std::string body = catalog_json(12);
    f.serve(body, "\"abc\"");
    const auto result = f.remote(CatalogFetch::Revalidate);
    REQUIRE(result);
    CHECK(result->origin == CatalogOrigin::Remote);
    CHECK(result->catalog.serial == 12);
    REQUIRE(result->catalog.entries.size() == 2);
    CHECK(result->warnings.empty());
    CHECK(f.persisted == std::vector<u64>{12});

    CHECK(f.fs.text(f.cache_body) == body);
    CHECK(f.fs.text(f.cache_body.string() + ".sig") == f.signer.signature_file(body));
    CHECK(f.fs.text(f.cache_body.string() + ".etag") == "\"abc\"");
    const auto requests = f.transport.requests();
    REQUIRE(requests.size() == 2);
    CHECK(header_of(requests[0], "If-None-Match") == nullptr);

    const auto again = f.remote(CatalogFetch::CacheOnly);
    REQUIRE(again);
    CHECK(again->catalog == result->catalog);
}

TEST_CASE("a verified cache revalidates with If-None-Match and a 304 yields it", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7), "\"v1\"");
    f.transport.route("GET", kCatalogUrl, answer(304));
    const auto result = f.remote(CatalogFetch::Revalidate);
    REQUIRE(result);
    CHECK(result->origin == CatalogOrigin::Cache);
    CHECK(result->catalog.serial == 7);
    const auto requests = f.transport.requests();
    REQUIRE(requests.size() == 1);
    const std::string* etag = header_of(requests[0], "If-None-Match");
    REQUIRE(etag != nullptr);
    CHECK(*etag == "\"v1\"");
}

TEST_CASE("an ETag that is not visible ASCII is neither sent nor stored", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7), "\"v1\"\r\nX-Injected: 1");
    f.serve(catalog_json(8), "\"v2\"\n");
    const auto result = f.remote(CatalogFetch::Revalidate);
    REQUIRE(result);
    CHECK(result->catalog.serial == 8);
    const auto requests = f.transport.requests();
    REQUIRE_FALSE(requests.empty());
    CHECK(header_of(requests[0], "If-None-Match") == nullptr);
    CHECK(f.fs.text(f.cache_body.string() + ".etag") == "");
}

TEST_CASE("a cache that fails to verify sends no If-None-Match", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7));
    f.fs.write_text(f.cache_body.string() + ".sig", "ed25519 broken");
    f.serve(catalog_json(8));
    const auto result = f.remote(CatalogFetch::Revalidate);
    REQUIRE(result);
    CHECK(result->origin == CatalogOrigin::Remote);
    CHECK(result->catalog.serial == 8);
    const auto requests = f.transport.requests();
    REQUIRE_FALSE(requests.empty());
    CHECK(header_of(requests[0], "If-None-Match") == nullptr);
}

TEST_CASE("a rejected body never touches the cache", "[catalog][remote]") {
    const std::string cached = catalog_json(7);

    SECTION("bad signature") {
        Fixture f;
        f.cache(cached);
        f.transport.route("GET", kCatalogUrl, answer(200, catalog_json(8)));
        f.transport.route("GET", kSignatureUrl, answer(200, f.signer.signature_file(catalog_json(9))));
        const auto result = f.remote(CatalogFetch::Revalidate);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::Untrusted);
        CHECK(f.fs.text(f.cache_body) == cached);
        CHECK(f.fs.text(f.cache_body.string() + ".etag") == "\"v1\"");
        // Only the cached copy's serial was admitted.
        CHECK(f.persisted == std::vector<u64>{7});
    }
    SECTION("unknown schema") {
        Fixture f;
        f.cache(cached);
        f.serve(catalog_json(8, 2));
        const auto result = f.remote(CatalogFetch::Revalidate);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::UnknownSchema);
        CHECK(result.error().schema == 2);
        CHECK(f.fs.text(f.cache_body) == cached);
    }
    SECTION("serial rollback") {
        Fixture f(7);
        f.cache(cached);
        f.serve(catalog_json(5));
        const auto result = f.remote(CatalogFetch::Revalidate);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::Untrusted);
        REQUIRE(result.error().cause);
        CHECK(result.error().cause->id == "trust.serial_rollback");
        CHECK(f.fs.text(f.cache_body) == cached);
        CHECK(f.serials.highest_seen() == 7);
    }
}

TEST_CASE("a non-200 status names the URL that returned it", "[catalog][remote]") {
    SECTION("catalog body") {
        Fixture f;
        f.transport.route("GET", kCatalogUrl, answer(404));
        const auto result = f.remote(CatalogFetch::Revalidate);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::HttpStatus);
        CHECK(result.error().http_status == 404);
        CHECK(result.error().url == kCatalogUrl);
    }
    SECTION("signature") {
        Fixture f;
        f.transport.route("GET", kCatalogUrl, answer(200, catalog_json(3)));
        f.transport.route("GET", kSignatureUrl, answer(403));
        const auto result = f.remote(CatalogFetch::Revalidate);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::HttpStatus);
        CHECK(result.error().http_status == 403);
        CHECK(result.error().url == kSignatureUrl);
        CHECK_FALSE(f.fs.exists(f.cache_body));
    }
    SECTION("304 without a cached copy") {
        Fixture f;
        f.transport.route("GET", kCatalogUrl, answer(304));
        const auto result = f.remote(CatalogFetch::Revalidate);
        REQUIRE_FALSE(result);
        CHECK(result.error().http_status == 304);
    }
}

TEST_CASE("a transport failure is FetchFailed with the net diagnostic as cause", "[catalog][remote]") {
    Fixture f;
    FakeHttpResponse refused;
    refused.error = make_diag(ErrorDomain::Net, MessageId{"net.test_refused"}).build();
    f.transport.route("GET", kCatalogUrl, refused);

    std::optional<CatalogLoadResult> out;
    f.source->load(CatalogFetch::Revalidate, {}, [&](CatalogLoadResult result) { out = std::move(result); });
    // The client retries a connect failure after a delay.
    while (!out) {
        f.strand.run_until([&] { return out.has_value() || f.strand.timed_pending() > 0; });
        if (!out) f.strand.advance(10s);
    }
    REQUIRE_FALSE(*out);
    CHECK(out->error().code == CatalogErrorCode::FetchFailed);
    CHECK(out->error().url == kCatalogUrl);
    CHECK(out->error().cause.has_value());
}

TEST_CASE("a cancelled load completes once with a failure and leaves the cache alone", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7));
    FakeHttpResponse slow = answer(200, catalog_json(8));
    slow.delay = 5s;
    f.transport.route("GET", kCatalogUrl, slow);

    CancelSource cancel;
    std::optional<CatalogLoadResult> out;
    int calls = 0;
    f.source->load(CatalogFetch::Revalidate, cancel.token(), [&](CatalogLoadResult result) {
        ++calls;
        out = std::move(result);
    });
    f.strand.run_until([&] { return f.transport.in_flight() > 0; });
    cancel.cancel(CancelReason::User);
    f.strand.run_until([&] { return out.has_value(); });
    f.strand.advance(10s);
    CHECK(calls == 1);
    REQUIRE_FALSE(*out);
    CHECK(out->error().code == CatalogErrorCode::FetchFailed);
    CHECK(f.fs.text(f.cache_body) == catalog_json(7));
}

TEST_CASE("a source destroyed mid-load never calls back", "[catalog][remote]") {
    Fixture f;
    FakeHttpResponse slow = answer(200, catalog_json(8));
    slow.delay = 5s;
    f.transport.route("GET", kCatalogUrl, slow);

    int calls = 0;
    f.source->load(CatalogFetch::Revalidate, {}, [&](CatalogLoadResult) { ++calls; });
    f.strand.run_until([&] { return f.transport.in_flight() > 0; });
    f.source.reset();
    f.strand.advance(10s);
    CHECK(calls == 0);
}

TEST_CASE("a failed cache write still yields the verified catalog, with a warning", "[catalog][remote]") {
    Fixture f;
    f.fs.faults().fail_always(testing::FsOperation::AtomicReplace,
                              make_diag(ErrorDomain::Storage, MessageId{"storage.test_full"}).build());
    f.serve(catalog_json(4));
    const auto result = f.remote(CatalogFetch::Revalidate);
    REQUIRE(result);
    CHECK(result->origin == CatalogOrigin::Remote);
    CHECK(result->catalog.serial == 4);
    REQUIRE(result->warnings.size() == 1);
    CHECK(result->warnings[0].id == "catalog.cache_write_failed");
    CHECK(result->warnings[0].severity == Severity::Warning);
}

TEST_CASE("an overlapping remote load is refused at once", "[catalog][remote]") {
    Fixture f;
    f.cache(catalog_json(7));
    std::optional<CatalogLoadResult> first;
    f.source->load(CatalogFetch::CacheOnly, {}, [&](CatalogLoadResult result) { first = std::move(result); });

    std::optional<CatalogLoadResult> second;
    f.source->load(CatalogFetch::CacheOnly, {}, [&](CatalogLoadResult result) { second = std::move(result); });
    REQUIRE(second);
    REQUIRE_FALSE(*second);
    REQUIRE(second->error().cause);
    CHECK(second->error().cause->id == "internal.bug");

    f.strand.run_until([&] { return first.has_value(); });
    CHECK(first->has_value());
}

TEST_CASE("the bundled catalog verifies like a download", "[catalog][bundled]") {
    Fixture f;
    const std::string body = catalog_json(3);
    f.fs.write_text(f.install.bundled_catalog, body);
    f.fs.write_text(f.install.bundled_catalog.string() + ".sig", f.signer.signature_file(body));
    const auto result = f.load(*f.bundled, CatalogFetch::Revalidate);
    REQUIRE(result);
    CHECK(result->origin == CatalogOrigin::Bundled);
    CHECK(result->catalog.serial == 3);
    CHECK(f.transport.requests().empty());
    // The serial guard covers remote copies only.
    CHECK(f.persisted.empty());
}

TEST_CASE("every bundled failure is BundledUnusable", "[catalog][bundled]") {
    Fixture f;
    const std::string body = catalog_json(3);

    SECTION("missing") {
        const auto result = f.load(*f.bundled, CatalogFetch::CacheOnly);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::BundledUnusable);
        CHECK(result.error().path == f.install.bundled_catalog);
        CHECK(to_diagnostic(result.error()).id == "catalog.bundled_unusable");
    }
    SECTION("signature missing") {
        f.fs.write_text(f.install.bundled_catalog, body);
        const auto result = f.load(*f.bundled, CatalogFetch::CacheOnly);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::BundledUnusable);
    }
    SECTION("signed by another key") {
        const test::TestSigner stranger;
        f.fs.write_text(f.install.bundled_catalog, body);
        f.fs.write_text(f.install.bundled_catalog.string() + ".sig", stranger.signature_file(body));
        const auto result = f.load(*f.bundled, CatalogFetch::CacheOnly);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CatalogErrorCode::BundledUnusable);
        REQUIRE(result.error().cause);
        CHECK(result.error().cause->id == "catalog.untrusted");
    }
    SECTION("malformed") {
        const std::string broken = R"({"schema":1,"serial":1})";
        f.fs.write_text(f.install.bundled_catalog, broken);
        f.fs.write_text(f.install.bundled_catalog.string() + ".sig", f.signer.signature_file(broken));
        const auto result = f.load(*f.bundled, CatalogFetch::CacheOnly);
        REQUIRE_FALSE(result);
        REQUIRE(result.error().cause);
        CHECK(result.error().cause->id == "catalog.malformed");
    }
}
