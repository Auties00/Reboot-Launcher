#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "engine_environment.hpp"

using rb::DataRoot;
using rb::NativePath;
using rb::os_linux::ipc::engine_environment;
using rb::os_linux::ipc::EngineEnvironmentInputs;
using rb::os_linux::ipc::pinned_engine_variables;

namespace {

const EngineEnvironmentInputs kInputs{
    NativePath{"/home/ada"}, "ada", "/bin/bash", NativePath{"/home/ada/.local/share"}, NativePath{"/home/ada/.cache"},
    NativePath{"/home/ada/.local/state"},
};
const DataRoot kDefaultRoot{NativePath{"/home/ada/.local/share/reboot-launcher"}, false};

using Entries = std::vector<std::string>;

const Entries kFixedPrefix{
    "HOME=/home/ada",
    "USER=ada",
    "LOGNAME=ada",
    "SHELL=/bin/bash",
    "XDG_DATA_HOME=/home/ada/.local/share",
    "XDG_CACHE_HOME=/home/ada/.cache",
    "XDG_STATE_HOME=/home/ada/.local/state",
};

Entries with_prefix(const Entries& rest) {
    Entries entries = kFixedPrefix;
    entries.insert(entries.end(), rest.begin(), rest.end());
    return entries;
}

}  // namespace

TEST_CASE("the XDG homes are pinned as the client resolved them", "[engine_environment]") {
    CHECK(pinned_engine_variables(kInputs, kDefaultRoot) ==
          Entries{"XDG_DATA_HOME=/home/ada/.local/share", "XDG_CACHE_HOME=/home/ada/.cache",
                  "XDG_STATE_HOME=/home/ada/.local/state"});
}

TEST_CASE("an overridden root is pinned too", "[engine_environment]") {
    const DataRoot root{NativePath{"/games/reboot"}, true};
    const Entries pinned = pinned_engine_variables(kInputs, root);
    REQUIRE(pinned.size() == 4);
    CHECK(pinned.back() == "REBOOT_LAUNCHER_HOME=/games/reboot");
}

TEST_CASE("only the allowed variables of the client reach the engine", "[engine_environment]") {
    const std::vector<std::string_view> inherited{
        "PATH=/usr/bin:/bin",
        "LD_PRELOAD=/tmp/hook.so",
        "LD_LIBRARY_PATH=/opt/lib",
        "APPDIR=/tmp/.mount_x",
        "APPIMAGE=/home/ada/r.AppImage",
        "OWD=/home/ada",
        "SteamAppId=0",
        "SSH_AUTH_SOCK=/tmp/ssh",
        "TERM=xterm",
        "LISTEN_FDS=1",
        "REBOOT_LAUNCHER_HOME=/elsewhere",
        "HOME=/elsewhere",
        "XDG_DATA_HOME=/elsewhere",
        "TMPDIR=/var/tmp",
        "XDG_DATA_DIRS=/usr/share",
        "XDG_CONFIG_DIRS=/etc/xdg",
        "XDG_CONFIG_HOME=/home/ada/.config",
        "XDG_RUNTIME_DIR=/run/user/1000",
        "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus",
        "LANG=it_IT.UTF-8",
        "LC_TIME=en_GB.UTF-8",
        "DISPLAY=:0",
    };
    CHECK(engine_environment(inherited, kInputs, kDefaultRoot) ==
          with_prefix({
              "PATH=/usr/bin:/bin",
              "TMPDIR=/var/tmp",
              "XDG_DATA_DIRS=/usr/share",
              "XDG_CONFIG_DIRS=/etc/xdg",
              "XDG_CONFIG_HOME=/home/ada/.config",
              "XDG_RUNTIME_DIR=/run/user/1000",
              "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus",
              "LANG=it_IT.UTF-8",
              "LC_TIME=en_GB.UTF-8",
          }));
}

TEST_CASE("relative directories and empty values are dropped", "[engine_environment]") {
    const std::vector<std::string_view> inherited{"XDG_RUNTIME_DIR=run/user/1000", "XDG_CONFIG_HOME=.config", "LANG=",
                                                  "TMPDIR=/tmp"};
    CHECK(engine_environment(inherited, kInputs, kDefaultRoot) ==
          with_prefix({"TMPDIR=/tmp", "PATH=/usr/local/bin:/usr/bin:/bin"}));
    const std::vector<std::string_view> relative_tmp{"TMPDIR=tmp"};
    CHECK(engine_environment(relative_tmp, kInputs, kDefaultRoot) ==
          with_prefix({"PATH=/usr/local/bin:/usr/bin:/bin"}));
}

TEST_CASE("the first of a name wins, even when it is dropped", "[engine_environment]") {
    const std::vector<std::string_view> inherited{"PATH=/a", "PATH=/b", "XDG_RUNTIME_DIR=relative",
                                                  "XDG_RUNTIME_DIR=/run/user/1000", "=x", "NOEQUALS"};
    CHECK(engine_environment(inherited, kInputs, kDefaultRoot) == with_prefix({"PATH=/a"}));
}

TEST_CASE("an overridden root replaces an inherited REBOOT_LAUNCHER_HOME", "[engine_environment]") {
    const std::vector<std::string_view> inherited{"REBOOT_LAUNCHER_HOME=/elsewhere"};
    const DataRoot root{NativePath{"/games/reboot"}, true};
    CHECK(engine_environment(inherited, kInputs, root) ==
          with_prefix({"REBOOT_LAUNCHER_HOME=/games/reboot", "PATH=/usr/local/bin:/usr/bin:/bin"}));
}
