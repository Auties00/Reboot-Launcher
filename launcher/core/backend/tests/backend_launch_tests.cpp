#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "backend_test_support.hpp"
#include "reboot/backend/backend_event.hpp"
#include "reboot/backend/backend_launch.hpp"
#include "reboot/backend/backend_service.hpp"
#include "reboot/backend/backend_sessions.hpp"
#include "reboot/backend/remote_backend_probe.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"

using namespace rb;
using namespace rb::backend;
using namespace rb::backend::test;

namespace {

[[nodiscard]] BackendLaunch launch() {
    BackendLaunch out;
    out.exe = testing::default_fake_root() / "app" / "reboot-backend";
    out.content_dir = testing::default_fake_root() / "app" / "backend-content";
    out.data_dir = backend_data_dir();
    out.user_environment.vars = {{"HOME", "/home/player"}, {"REBOOT_STRAY", "1"}, {"LD_PRELOAD", "evil.so"}};
    return out;
}

[[nodiscard]] std::optional<std::string> env(const process::ProcessSpec& spec, std::string_view name) {
    for (const auto& [key, value] : spec.env.vars().vars)
        if (key == name) return value;
    return std::nullopt;
}

struct NoSessions final : IBackendSessions {
    void stop_sessions(std::vector<SessionId>, UniqueFunction<void(Result<void>)> done) override { done({}); }
};

}  // namespace

TEST_CASE("the backend runs with --control=stdio in its data directory", "[backend][launch]") {
    const Result<process::ProcessSpec> spec = make_backend_spec(launch());
    REQUIRE(spec);
    CHECK(spec->role == process::ChildRole::Backend);
    CHECK_FALSE(spec->session);
    CHECK(spec->exe == launch().exe);
    CHECK(spec->args == std::vector<std::string>{"--control=stdio"});
    CHECK(spec->working_directory() == backend_data_dir());
    CHECK(spec->stdio == ports::StdioMode::ControlChannel);
    CHECK(env(*spec, kBackendDataEnv) == display_utf8(backend_data_dir()));
    CHECK(env(*spec, kBackendContentEnv) == display_utf8(launch().content_dir));
    CHECK(env(*spec, "HOME") == "/home/player");
    CHECK_FALSE(env(*spec, "REBOOT_STRAY"));
    CHECK_FALSE(env(*spec, "LD_PRELOAD"));
}

TEST_CASE("a path that is not valid Unicode is refused, not mangled", "[backend][launch]") {
    BackendLaunch bad = launch();
    NativePath::string_type name = bad.data_dir.native();
    name.push_back(static_cast<NativePath::value_type>(sizeof(NativePath::value_type) == 1 ? 0xFF : 0xD800));
    bad.data_dir = NativePath(name);
    const Result<process::ProcessSpec> spec = make_backend_spec(bad);
    REQUIRE_FALSE(spec);
    CHECK(spec.error().id == "backend.path_not_utf8");
}

TEST_CASE("a relative executable is refused", "[backend][launch]") {
    BackendLaunch relative = launch();
    relative.exe = NativePath("reboot-backend");
    const Result<process::ProcessSpec> spec = make_backend_spec(relative);
    REQUIRE_FALSE(spec);
    CHECK(spec.error().id == "process.spec_relative_path");
}

TEST_CASE("a moved lease is released once, by its last owner", "[backend][lease]") {
    ProcessHarness h;
    testing::FakeHttpTransport transport{h.rt.strand(), h.rt.clock()};
    testing::FakeRandom random{1};
    net::HostTlsMemory tls{{}, nullptr};
    net::HttpClient http{transport, tls, h.rt.strand(), h.rt.timers(), random};
    RemoteBackendProbe probe{http};
    NoSessions sessions;
    BackendService service(BackendConfig{}, *h.process, probe, sessions, h.rt.ops(), h.rt.requests(), tls, h.rt.events(),
                           h.rt.strand(), h.rt.timers());

    BackendLease empty;
    CHECK_FALSE(empty.active());
    empty.release();

    BackendLease first = service.acquire(session_id(1)).value();
    BackendLease moved(std::move(first));
    CHECK_FALSE(first.active());
    CHECK(moved.active());
    CHECK(moved.session() == session_id(1));
    CHECK(moved.config() == BackendConfig{});
    CHECK(service.state().leases == 1);

    BackendLease other = service.acquire(session_id(2)).value();
    CHECK(service.state().leases == 2);
    other = std::move(moved);
    CHECK(service.state().leases == 1);
    CHECK(other.session() == session_id(1));
    other.release();
    other.release();
    CHECK(service.state().leases == 0);

    BackendLease maintenance = service.acquire_maintenance().value();
    CHECK_FALSE(maintenance.session());
    CHECK(service.session_leases() == 0);
    CHECK(service.state().leases == 1);
    h.rt.run_until_idle();
}

TEST_CASE("a backend event sizes what it carries", "[backend][event]") {
    BackendEvent small;
    BackendEvent large;
    large.state.version = std::string(512, 'v');
    large.state.last_error =
        make_diag(ErrorDomain::Backend, MessageId{"backend.crashed"}).detail(std::string(256, 'd')).build();
    CHECK(small.approx_bytes() >= sizeof(BackendEvent));
    CHECK(large.approx_bytes() >= small.approx_bytes() + 512 + 256);
}
