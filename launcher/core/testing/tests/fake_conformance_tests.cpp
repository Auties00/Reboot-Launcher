#include <array>
#include <optional>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/ports/secret_store.hpp"
#include "reboot/testing/conformance_report.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_caller_context.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_loopback_peer_inspector.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_port_inspector.hpp"
#include "reboot/testing/fake_prereqs.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_registrar.hpp"
#include "reboot/testing/fake_runner_platform.hpp"
#include "reboot/testing/fake_secret_store.hpp"
#include "reboot/testing/fake_system_info.hpp"
#include "reboot/testing/manual_waiter.hpp"
#include "reboot/testing/port_binder.hpp"
#include "reboot/testing/port_conformance.hpp"

using namespace reboot;
using namespace reboot::testing;

namespace {

void require_passed(const ConformanceReport& report) {
    INFO(report.describe());
    REQUIRE(report.passed());
}

}  // namespace

// The drift guard: each fake passes the suite its real adapters run.
TEST_CASE("FakeRandom passes the random suite", "[testing][conformance]") {
    FakeRandom random;
    require_passed(run_random_conformance(random));
}

TEST_CASE("FakePlatformPaths passes the platform paths suite", "[testing][conformance]") {
    FakePlatformPaths paths;
    require_passed(run_platform_paths_conformance(paths));

    paths.set_install_kind(ports::InstallKind::Velopack);
    paths.set_velopack_package_dir(paths.base() / "velopack");
    require_passed(run_platform_paths_conformance(paths));

    paths.set_install_kind(ports::InstallKind::AppBundle);
    require_passed(run_platform_paths_conformance(paths));
    paths.set_install_kind(ports::InstallKind::Portable);
    CHECK_FALSE(run_platform_paths_conformance(paths).passed());
    paths.set_install_kind(ports::InstallKind::Velopack);
    paths.set_velopack_package_dir(std::nullopt);
    CHECK_FALSE(run_platform_paths_conformance(paths).passed());
}

TEST_CASE("FakeSecretStore passes the secret store suite", "[testing][conformance]") {
    FakeRandom random;
    FakeSecretStore store;
    require_passed(run_secret_store_conformance(store, random));
    CHECK(store.keys().empty());

    FakeSecretStore unavailable(ports::SecretStoreKind::Unavailable);
    require_passed(run_secret_store_conformance(unavailable, random));
}

TEST_CASE("FakeCallerContext passes the caller context suite", "[testing][conformance]") {
    FakeCallerContext caller({"1", false, true, {{"DISPLAY", ":0"}, {"WAYLAND_DISPLAY", "wayland-0"}}});
    require_passed(run_caller_context_conformance(caller, 4242));
}

TEST_CASE("the fake inspectors pass the inspector suites", "[testing][conformance]") {
    FakePortInspector ports;
    FakeLoopbackPeerInspector supported(true);
    const auto binder = make_fake_port_binder(ports, supported, 4242, 1000);
    require_passed(run_port_inspector_conformance(ports, *binder));
    require_passed(run_loopback_peer_inspector_conformance(supported, *binder, 1000u));

    FakeLoopbackPeerInspector unsupported(false);
    const auto unsupported_binder = make_fake_port_binder(ports, unsupported, 4242, 1000);
    require_passed(run_loopback_peer_inspector_conformance(unsupported, *unsupported_binder, std::nullopt));
}

TEST_CASE("FakeRunnerPlatform passes the runner suite in each OS shape", "[testing][conformance]") {
    const NativePath runtime = default_fake_root() / "runtimes" / "r1";
    const NativePath prefix = default_fake_root() / "prefix";
    const NativePath winhost = default_fake_root() / "app" / "reboot-winhost.exe";

    FakeRunnerPlatform mac({ports::RunnerKind::MacRuntime});
    mac.set_layout(ports::RunnerKind::MacRuntime, {"files", NativePath("files") / "bin" / "wine", {}, "drive_c"});
    require_passed(run_runner_platform_conformance(mac, {ports::RunnerKind::MacRuntime, {runtime}, prefix, winhost}));
    CHECK(mac.post_extracted().size() == 2);

    FakeRunnerPlatform linux_shape({ports::RunnerKind::Umu, ports::RunnerKind::Wine});
    linux_shape.set_layout(ports::RunnerKind::Umu, {"proton", "umu-run", {}, "drive_c"});
    const NativePath launcher = default_fake_root() / "runtimes" / "umu1";
    require_passed(
        run_runner_platform_conformance(linux_shape, {ports::RunnerKind::Umu, {runtime, launcher}, prefix, winhost}));
    CHECK(linux_shape.post_extracted() == std::vector{runtime, runtime, launcher, launcher});
    const auto umu = linux_shape.layout(ports::RunnerKind::Umu, {runtime, launcher});
    REQUIRE(umu);
    CHECK(umu->entry == launcher / "umu-run");
}

TEST_CASE("FakeDiskInfo passes the disk info suite", "[testing][conformance]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    FakeDiskInfo disk;
    disk.add_volume({default_fake_root(), "root", "test", 10, 100});
    disk.add_volume({default_fake_root() / "scratch", "scratch", "test", 5, 50});
    require_passed(run_disk_info_conformance(disk, {waiter, default_fake_root() / "scratch" / "dir"}));
    CHECK(disk.volume_of(default_fake_root() / "scratch" / "dir")->label == "scratch");
}

TEST_CASE("the OS service fakes pass their suites", "[testing][conformance]") {
    FakeRegistrar registrar;
    const std::array kinds{ports::IntegrationKind::UrlScheme, ports::IntegrationKind::Autostart};
    require_passed(run_integration_registrar_conformance(registrar, default_fake_root() / "app" / "reboot", kinds));

    FakePrereqs prereqs;
    prereqs.set({{"rosetta", false, MessageId{"platform.rosetta_missing"}}, {"vulkan", true, std::nullopt}});
    require_passed(run_prerequisite_probe_conformance(prereqs));

    FakeSystemInfo system;
    require_passed(run_system_info_conformance(system));
}
