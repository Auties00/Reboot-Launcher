#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "reboot/backend/remote_backend_probe.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace rb;
using namespace rb::backend;

namespace {

struct Probe {
    explicit Probe(std::vector<storage::UpstreamTlsMemory> loaded = {}) : tls(std::move(loaded), nullptr) {}

    Result<BackendInfo> run(const BackendUrl& url, CancelToken token = {}) {
        std::optional<Result<BackendInfo>> result;
        probe.probe(url, std::move(token), [&](Result<BackendInfo> info) { result = std::move(info); });
        rt.run_until_idle();
        REQUIRE(result);
        return std::move(*result);
    }

    void route(const std::string& url, u32 status, std::string body = {}) {
        testing::FakeHttpResponse response;
        response.status = status;
        response.body.assign(body.begin(), body.end());
        transport.route("GET", url, std::move(response));
    }

    testing::DeterministicRuntime rt;
    testing::FakeHttpTransport transport{rt.strand(), rt.clock()};
    testing::FakeRandom random{3};
    net::HostTlsMemory tls;
    net::HttpClient http{transport, tls, rt.strand(), rt.timers(), random};
    RemoteBackendProbe probe{http};
};

const BackendUrl kRemote{std::nullopt, "play.example", Port{3551}};

}  // namespace

TEST_CASE("a Reboot backend-info answer over https is read in full", "[backend][probe]") {
    Probe p;
    p.route("https://play.example:3551/reboot/v1/backend-info", 200,
            R"({"impl":"reboot","api_version":1,"version":"2.1.0","ws_port":8443})");
    const Result<BackendInfo> info = p.run(kRemote);
    REQUIRE(info);
    CHECK(info->url.scheme == net::UrlScheme::Https);
    CHECK(info->flavor == identity::UpstreamFlavor::Reboot);
    CHECK(info->version == "2.1.0");
    CHECK(info->api_version == 1u);
    CHECK(info->ws_port == Port{8443});
    REQUIRE(p.transport.requests().size() == 1);
}

TEST_CASE("any status means reachable, and only impl reboot means Reboot", "[backend][probe]") {
    Probe p;
    p.route("https://play.example:3551/reboot/v1/backend-info", 404, "<html>not found</html>");
    const Result<BackendInfo> info = p.run(kRemote);
    REQUIRE(info);
    CHECK(info->flavor == identity::UpstreamFlavor::ThirdParty);
    CHECK_FALSE(info->version);
    CHECK_FALSE(info->ws_port);

    p.transport.clear_routes();
    p.route("https://play.example:3551/reboot/v1/backend-info", 200, R"({"impl":"reboot","api_version":99,"ws_port":0})");
    const Result<BackendInfo> newer = p.run(kRemote);
    REQUIRE(newer);
    CHECK(newer->flavor == identity::UpstreamFlavor::Reboot);
    CHECK(newer->api_version == 99u);
    CHECK_FALSE(newer->ws_port);
}

TEST_CASE("without a scheme a failed https falls back to http, whose refusal comes back unchanged", "[backend][probe]") {
    Probe p;
    const Result<BackendInfo> refused = p.run(kRemote);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "net.plain_http_needs_consent");

    p.tls.remember_http_acknowledged("play.example");
    p.route("http://play.example:3551/reboot/v1/backend-info", 200, "{}");
    const Result<BackendInfo> plain = p.run(kRemote);
    REQUIRE(plain);
    CHECK(plain->url.scheme == net::UrlScheme::Http);
    CHECK(plain->url.origin() == "http://play.example:3551");
}

TEST_CASE("a host seen on https is never probed over http", "[backend][probe]") {
    Probe p({storage::UpstreamTlsMemory{"play.example", true, false}});
    const Result<BackendInfo> refused = p.run(BackendUrl{net::UrlScheme::Http, "play.example", Port{3551}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().id == "net.https_downgrade_refused");
    CHECK(p.transport.requests().empty());
}

TEST_CASE("an upstream that answers on neither scheme is unreachable", "[backend][probe]") {
    Probe p;
    const Result<BackendInfo> info = p.run(BackendUrl{std::nullopt, "127.0.0.1", Port{3551}});
    REQUIRE_FALSE(info);
    CHECK(info.error().id == "backend.unreachable");
    CHECK(info.error().retryable);
    CHECK(info.error().causes.size() == 2);
    CHECK(p.transport.requests().size() == 2);
}

TEST_CASE("a cancelled probe does not fall back", "[backend][probe]") {
    Probe p;
    CancelSource cancel;
    cancel.cancel(CancelReason::User);
    const Result<BackendInfo> info = p.run(kRemote, cancel.token());
    REQUIRE_FALSE(info);
    CHECK(info.error().id != "backend.unreachable");
    CHECK(p.transport.requests().size() <= 1);
}
