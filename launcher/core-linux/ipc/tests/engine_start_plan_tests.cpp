#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "engine_start_plan.hpp"
#include "systemd_unit_state.hpp"

using rb::NativePath;
using rb::os_linux::ipc::engine_command;
using rb::os_linux::ipc::show_unit_argv;
using rb::os_linux::ipc::socket_unit_action;
using rb::os_linux::ipc::socket_unit_argv;
using rb::os_linux::ipc::SocketUnitAction;
using rb::os_linux::ipc::systemd_run_argv;
using rb::os_linux::ipc::SystemdUnitState;
using rb::os_linux::ipc::transient_unit_name;

namespace {

using Argv = std::vector<std::string>;

const NativePath kSocket{"/run/user/1000/reboot-launcher/0123456789abcdef.sock"};

SystemdUnitState unit(bool loaded, bool running, std::vector<NativePath> paths) {
    return {.loaded = loaded, .running = running, .stream_paths = std::move(paths)};
}

}  // namespace

TEST_CASE("the socket unit is started only when it serves exactly the client's socket", "[engine_start_plan]") {
    CHECK(socket_unit_action(unit(true, false, {kSocket}), kSocket, false) == SocketUnitAction::Start);
    CHECK(socket_unit_action(unit(true, true, {kSocket}), kSocket, true) == SocketUnitAction::Start);
    CHECK(socket_unit_action(unit(false, false, {kSocket}), kSocket, false) == SocketUnitAction::Skip);
    CHECK(socket_unit_action(unit(true, false, {}), kSocket, false) == SocketUnitAction::Skip);
    CHECK(socket_unit_action(unit(true, false, {NativePath{"/run/user/1000/reboot-launcher/fedcba9876543210.sock"}}),
                             kSocket, false) == SocketUnitAction::Skip);
    CHECK(socket_unit_action(unit(true, false, {kSocket, NativePath{"/run/user/1000/other.sock"}}), kSocket, false) ==
          SocketUnitAction::Skip);
}

TEST_CASE("an active socket unit whose socket a foreground engine unlinked is restarted", "[engine_start_plan]") {
    CHECK(socket_unit_action(unit(true, true, {kSocket}), kSocket, false) == SocketUnitAction::Restart);
}

TEST_CASE("systemctl calls run against the user manager", "[engine_start_plan]") {
    CHECK(show_unit_argv("reboot-engine.socket") ==
          Argv{"systemctl", "--user", "show", "--property=LoadState,ActiveState,Listen", "reboot-engine.socket"});
    CHECK(socket_unit_argv(SocketUnitAction::Start) == Argv{"systemctl", "--user", "start", "reboot-engine.socket"});
    CHECK(socket_unit_argv(SocketUnitAction::Restart) ==
          Argv{"systemctl", "--user", "restart", "reboot-engine.socket"});
}

TEST_CASE("the engine runs on demand, through the AppImage when there is one", "[engine_start_plan]") {
    const NativePath engine{"/opt/rl/versions/1.0.0/reboot-engine"};
    CHECK(engine_command(engine, std::nullopt) ==
          Argv{"/opt/rl/versions/1.0.0/reboot-engine", "run", "--origin=on-demand"});
    CHECK(engine_command(NativePath{"/tmp/.mount_rl/usr/bin/reboot-engine"}, NativePath{"/home/ada/rl.AppImage"}) ==
          Argv{"/home/ada/rl.AppImage", "engine", "run", "--origin=on-demand"});
}

TEST_CASE("systemd-run names one transient engine per data root and pins its environment", "[engine_start_plan]") {
    const std::string name = transient_unit_name("0123456789abcdef");
    CHECK(name == "reboot-engine-0123456789abcdef");
    const Argv pinned{"XDG_DATA_HOME=/home/ada/.local/share", "REBOOT_LAUNCHER_HOME=/data/rl"};
    const Argv command{"/opt/rl/reboot-engine", "run", "--origin=on-demand"};
    CHECK(systemd_run_argv(name, pinned, command) == Argv{"systemd-run",
                                                          "--user",
                                                          "--unit=reboot-engine-0123456789abcdef",
                                                          "--collect",
                                                          "--quiet",
                                                          "--setenv=XDG_DATA_HOME=/home/ada/.local/share",
                                                          "--setenv=REBOOT_LAUNCHER_HOME=/data/rl",
                                                          "--",
                                                          "/opt/rl/reboot-engine",
                                                          "run",
                                                          "--origin=on-demand"});
}
