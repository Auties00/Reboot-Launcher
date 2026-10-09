#pragma once

#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace rb::os_linux::ipc {

// What SystemdEngineStarter reads from `systemctl --user show --property=LoadState,ActiveState,Listen
// <unit>`, whose output is one Name=value line per property and per Listen entry.
struct SystemdUnitState {
    // LoadState=loaded: the unit file is installed.
    bool loaded = false;
    // ActiveState is active, activating or reloading.
    bool running = false;
    // Each "Listen=<path> (Stream)"; a socket unit's only.
    std::vector<NativePath> stream_paths;

    [[nodiscard]] static SystemdUnitState parse(std::string_view show_output);
};

}  // namespace rb::os_linux::ipc
