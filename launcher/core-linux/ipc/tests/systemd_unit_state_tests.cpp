#include <catch2/catch_test_macros.hpp>

#include "systemd_unit_state.hpp"

using reboot::NativePath;
using reboot::os_linux::ipc::SystemdUnitState;

TEST_CASE("an installed, listening socket unit", "[systemd_unit_state]") {
    const SystemdUnitState state = SystemdUnitState::parse(
        "Listen=/run/user/1000/reboot-launcher/0123456789abcdef.sock (Stream)\n"
        "LoadState=loaded\n"
        "ActiveState=active\n");
    CHECK(state.loaded);
    CHECK(state.running);
    REQUIRE(state.stream_paths.size() == 1);
    CHECK(state.stream_paths.front() == NativePath{"/run/user/1000/reboot-launcher/0123456789abcdef.sock"});
}

TEST_CASE("a unit systemd does not know", "[systemd_unit_state]") {
    const SystemdUnitState state = SystemdUnitState::parse("LoadState=not-found\nActiveState=inactive\n");
    CHECK_FALSE(state.loaded);
    CHECK_FALSE(state.running);
    CHECK(state.stream_paths.empty());
}

TEST_CASE("starting and reloading units count as running, failed ones do not", "[systemd_unit_state]") {
    CHECK(SystemdUnitState::parse("ActiveState=activating").running);
    CHECK(SystemdUnitState::parse("ActiveState=reloading").running);
    CHECK_FALSE(SystemdUnitState::parse("ActiveState=failed").running);
    CHECK_FALSE(SystemdUnitState::parse("ActiveState=deactivating").running);
}

TEST_CASE("only stream listeners are socket paths", "[systemd_unit_state]") {
    const SystemdUnitState state = SystemdUnitState::parse(
        "Listen=/run/a.sock (Stream)\nListen=/run/b.sock (Datagram)\nListen= (Stream)\nListen=/run/c.sock (Stream)");
    REQUIRE(state.stream_paths.size() == 2);
    CHECK(state.stream_paths[0] == NativePath{"/run/a.sock"});
    CHECK(state.stream_paths[1] == NativePath{"/run/c.sock"});
}
