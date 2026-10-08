#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "reboot/backend/backend_config.hpp"
#include "reboot/backend/backend_target.hpp"

using namespace reboot;
using namespace reboot::backend;

namespace {

storage::BackendTarget stored_with_every_kind() {
    storage::BackendTarget stored;
    stored.local = storage::LocalBackendAddress{HostPort{"127.0.0.1", Port{4000}}, HostPort{"127.0.0.1", Port{8080}}};
    stored.remote = storage::RemoteBackendAddress{storage::BackendScheme::Http, HostPort{"play.example", Port{3551}},
                                                  std::nullopt};
    return stored;
}

}  // namespace

TEST_CASE("the stored kind is selected with its own address", "[backend]") {
    storage::BackendTarget stored = stored_with_every_kind();
    stored.kind = storage::BackendKind::Remote;
    const Result<BackendTarget> target = BackendTarget::from_settings(stored);
    REQUIRE(target);
    CHECK(target->kind() == storage::BackendKind::Remote);
    const std::optional<BackendUrl> url = target->upstream_url();
    REQUIRE(url);
    CHECK(url->scheme == net::UrlScheme::Http);
    CHECK(url->origin() == "http://play.example:3551");
}

TEST_CASE("switching kinds keeps the addresses of the others", "[backend]") {
    storage::BackendTarget stored = stored_with_every_kind();
    const storage::BackendTarget before = stored;

    BackendTarget{EmbeddedBackend{}}.apply_to(stored);
    CHECK(stored.kind == storage::BackendKind::Embedded);
    CHECK(stored.local == before.local);
    CHECK(stored.remote == before.remote);

    BackendTarget{LocalBackend{HostPort{"127.0.0.1", Port{5000}}, std::nullopt}}.apply_to(stored);
    CHECK(stored.kind == storage::BackendKind::Local);
    CHECK(stored.local.endpoint.port == Port{5000});
    CHECK(stored.remote == before.remote);
}

TEST_CASE("a remote scheme survives a round trip through settings", "[backend]") {
    const Result<BackendUrl> url = BackendUrl::parse("https://play.example:443");
    REQUIRE(url);
    storage::BackendTarget stored;
    BackendTarget{RemoteBackend{*url, std::nullopt}}.apply_to(stored);
    REQUIRE(stored.remote);
    CHECK(stored.remote->scheme == storage::BackendScheme::Https);
    const Result<BackendTarget> back = BackendTarget::from_settings(stored);
    REQUIRE(back);
    CHECK(back->upstream_url() == url);
}

TEST_CASE("a remote kind without a remote address fails", "[backend]") {
    storage::BackendTarget stored;
    stored.kind = storage::BackendKind::Remote;
    const Result<BackendTarget> target = BackendTarget::from_settings(stored);
    REQUIRE_FALSE(target);
    CHECK(target.error().id == "backend.remote_address_missing");
}

TEST_CASE("the config carries allow_lan into the bind address", "[backend]") {
    storage::BackendSettings settings;
    settings.allow_lan = true;
    const Result<BackendConfig> config = BackendConfig::from_settings(settings);
    REQUIRE(config);
    CHECK(config->target.embedded());
    CHECK(config->bind_address() == kLanBindAddress);
}
