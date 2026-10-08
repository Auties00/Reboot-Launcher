#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"

namespace reboot::os_linux::ipc {

inline constexpr std::string_view kDefaultEnginePath = "/usr/local/bin:/usr/bin:/bin";

// From LinuxClientPaths: what the engine must resolve as this client did, so that it serves the
// same data root and therefore the same socket.
struct EngineEnvironmentInputs {
    NativePath home;
    std::string user_name;
    std::string login_shell;
    NativePath data_home;
    NativePath cache_home;
    NativePath state_home;
};

// "NAME=value" entries both engine starts pin, as systemd-run --setenv and in the self-spawn's
// envp: XDG_DATA_HOME, XDG_CACHE_HOME, XDG_STATE_HOME, and REBOOT_LAUNCHER_HOME=<root> when the
// root is overridden.
[[nodiscard]] std::vector<std::string> pinned_engine_variables(const EngineEnvironmentInputs& inputs,
                                                               const DataRoot& root);

// The self-spawned engine's whole envp: HOME, USER, LOGNAME and SHELL from `inputs`, the pinned
// variables, then from `inherited` ("NAME=value"; the first of a name wins, as in getenv) only
// non-empty PATH, TMPDIR, XDG_DATA_DIRS, XDG_CONFIG_DIRS, DBUS_SESSION_BUS_ADDRESS, LANG and LC_*,
// and XDG_CONFIG_HOME and XDG_RUNTIME_DIR when absolute. PATH defaults to kDefaultEnginePath.
[[nodiscard]] std::vector<std::string> engine_environment(std::span<const std::string_view> inherited,
                                                          const EngineEnvironmentInputs& inputs, const DataRoot& root);

}  // namespace reboot::os_linux::ipc
