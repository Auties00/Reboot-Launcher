#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "linux_ipc_test_support.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/os_linux/ipc/linux_client_paths.hpp"
#include "reboot/os_linux/ipc/systemd_engine_starter.hpp"
#include "reboot/os_linux/ipc/unix_socket_connector.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"

using namespace rb;
using namespace rb::os_linux::ipc;
using namespace rb::os_linux::ipc::test;
using namespace std::chrono_literals;

#if defined(REBOOT_FAKE_UNIX_ENGINE_EXE)
// Without $XDG_RUNTIME_DIR the runtime base is the /tmp fallback, so steps 1 and 2 are skipped and
// the starter self-spawns the fake engine: the double fork, its envp and spawn.lock end to end.
TEST_CASE("the engine starter self-spawns a detached engine on an overridden root", "[linux_ipc_conformance]") {
    const EnvOverride runtime{"XDG_RUNTIME_DIR", std::nullopt};
    // The fake engine exits at once when a REBOOT_ variable reaches it.
    const EnvOverride leak{"REBOOT_CONFORMANCE_LEAK", "1"};
    const testing::ScratchDir scratch = make_private_scratch("linux-engine-starter");
    Result<LinuxClientPaths> paths = LinuxClientPaths::detect();
    REQUIRE(paths);
    REQUIRE_FALSE(paths->runtime_base().from_xdg_runtime_dir);
    const NativePath runtime_base = paths->runtime_base().path;

    const ports::CallerContext caller{.os_session = "", .elevated = false, .interactive = true, .display_env = {}};
    SystemdEngineStarter starter{caller, std::move(*paths), false};
    const DataRoot root{scratch.path() / "home", true};
    const std::string endpoint =
        ports::endpoint_name(ports::PeerIdentity{std::to_string(own_uid()), 0}, root_hash16(canonical_root(root)));
    testing::EngineStarterSubject subject{NativePath{REBOOT_FAKE_UNIX_ENGINE_EXE}, root, [runtime_base, endpoint] {
                                              return UnixSocketConnector{runtime_base}.connect(endpoint, 200ms).has_value();
                                          }};
    testing::WallClockWaiter waiter;
    const testing::ConformanceReport report =
        testing::run_engine_starter_conformance(starter, std::move(subject), {waiter, scratch.path()});
    INFO(report.describe());
    CHECK(report.passed());
    CHECK(path_exists(root.root / "state" / "spawn.lock"));
}
#endif
