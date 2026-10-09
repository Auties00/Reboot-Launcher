#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/paths.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_client_platform.hpp"
#include "reboot/testing/fake_executables.hpp"
#include "reboot/testing/fake_platform.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/port_binder.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"

using namespace reboot;
using namespace reboot::testing;

namespace {

void require_passed(const ConformanceReport& report) {
    INFO(report.describe());
    REQUIRE(report.passed());
}

}  // namespace

TEST_CASE("FakePlatform takes each OS's shape", "[testing][platform]") {
    for (const FakeOs os : {FakeOs::Windows, FakeOs::MacOs, FakeOs::Linux}) {
        DeterministicRuntime runtime;
        FakePlatform platform(runtime, {os});
        ports::PlatformServices& services = platform.services();
        CHECK((services.session_host != nullptr) == (os == FakeOs::Windows));
        CHECK((services.runner != nullptr) == (os != FakeOs::Windows));
        CHECK((platform.session_host() != nullptr) == (os == FakeOs::Windows));
        const auto peer = platform.peer_inspector().peer_uid({}, {});
        CHECK(peer.has_value() == (os == FakeOs::Linux));
        CHECK(platform.fs().is_dir(platform.paths().default_data_root()));
        CHECK(platform.fs().is_dir(platform.paths().default_logs_root()));
        CHECK(services.ipc_listener != nullptr);
        if (os == FakeOs::MacOs) CHECK(platform.runner()->supported() == std::vector{ports::RunnerKind::MacRuntime});
        require_passed(run_system_info_conformance(platform.system()));
        require_passed(run_platform_paths_conformance(platform.paths()));
    }
}

TEST_CASE("FakePlatform's watcher follows its file system and its disk holds the root", "[testing][platform]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    FakePlatform platform(runtime);
    const NativePath logs = platform.paths().default_logs_root();
    require_passed(run_file_watcher_conformance(platform.watcher(), platform.fs(), {waiter, logs}));
    require_passed(run_disk_info_conformance(platform.disk(), {waiter, logs}));
    require_passed(run_shell_launcher_conformance(platform.shell(), platform.fs(), {waiter, logs}));
    require_passed(run_update_applier_conformance(platform.updater(), {waiter, logs}));
    require_passed(run_secret_store_conformance(platform.secrets(), platform.random()));

    // Staging a package that exists is recorded, and so is a restart.
    platform.fs().write_text(logs / "update.nupkg", "pk");
    REQUIRE(platform.updater().stage(logs / "update.nupkg"));
    REQUIRE(platform.updater().apply_and_restart({"--resume"}));
    CHECK(platform.updater().restarted_with() == std::vector<std::string>{"--resume"});
    CHECK(platform.shell().trashed().size() == 1);

    ports::PlatformServices taken = platform.take();
    CHECK(taken.fs != nullptr);
}

TEST_CASE("FakeClientPlatform reaches FakePlatform's engine through shared InMemoryIpc", "[testing][platform]") {
    DeterministicRuntime runtime;
    FakePlatform platform(runtime);
    FakeClientPlatform client(platform.ipc());
    std::size_t accepted = 0;
    REQUIRE(platform.services().ipc_listener->listen("engine", [&accepted](std::unique_ptr<ports::IByteStream>) { ++accepted; }));
    ports::ClientPlatform taken = client.take();
    REQUIRE(taken.connector->connect("engine", std::chrono::seconds{1}));
    runtime.run_until_idle();
    CHECK(accepted == 1);

    client.starter().script({ports::StartResult::AwaitingUser, ports::StartResult::Started});
    CHECK(*taken.starter->ensure_started({}, {}) == ports::StartResult::AwaitingUser);
    CHECK(*taken.starter->ensure_started({}, {}) == ports::StartResult::Started);
    CHECK(*taken.starter->ensure_started({}, {}) == ports::StartResult::Started);
    CHECK(client.starter().calls() == 3);

    CHECK(taken.self.user_id == platform.ipc().self().user_id);
    const NativePath marker = client.paths().default_data_root() / "marker";
    client.files().write_text(marker, "{}");
    CHECK(taken.revisions->revision(marker).has_value());
}

TEST_CASE("the OS service fakes pass their remaining suites", "[testing][conformance][platform]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);

    FakeSecurityProbe security;
    require_passed(run_security_probe_conformance(security, {waiter, default_fake_root()}));
    security.set(std::optional<ports::SecurityProducts>(ports::SecurityProducts{{"Defender", "Other AV"}, ports::SmartAppControl::On}));
    require_passed(run_security_probe_conformance(security, {waiter, default_fake_root()}));

    FakeEngineStarter starter;
    bool started = false;
    starter.script({ports::StartResult::Started, ports::StartResult::AlreadyRunning});
    starter.on_started([&started](const NativePath&, const DataRoot&) { started = true; });
    require_passed(run_engine_starter_conformance(
        starter, {default_fake_root() / "app" / "reboot-engine", {default_fake_root() / "data", false}, [&started] { return started; }},
        {waiter, default_fake_root()}));
}

TEST_CASE("ScratchDir makes a fresh directory and removes it with its contents", "[testing][platform]") {
    FakeRandom random(99);
    NativePath made;
    {
        auto scratch = ScratchDir::create(random, "reboot-kit-test");
        REQUIRE(scratch);
        made = scratch->path();
        CHECK(std::filesystem::is_directory(made));
        std::filesystem::create_directories(made / "nested" / "deeper");
        ScratchDir moved = std::move(*scratch);
        CHECK(moved.path() == made);
    }
    CHECK_FALSE(std::filesystem::exists(made));
}

TEST_CASE("the socket port binder holds and releases loopback ports", "[testing][platform]") {
    const auto binder = make_socket_port_binder();
    const auto tcp = binder->bind_tcp();
    REQUIRE(tcp);
    CHECK(tcp->is_loopback());
    CHECK(tcp->port.value != 0);
    const auto udp = binder->bind_udp();
    REQUIRE(udp);
    const auto pair = binder->connect_loopback();
    REQUIRE(pair);
    CHECK(pair->first.port != pair->second.port);
    binder->release(tcp->port);
    binder->release(*udp);
    CHECK(binder->owner_pid() != 0);
}

TEST_CASE("the fake executables are where the build put them", "[testing][platform]") {
    for (const auto& exe : {fake_backend_exe(), fake_game_server_exe(), fake_game_exe()})
        if (exe) CHECK(std::filesystem::is_regular_file(*exe));
}
