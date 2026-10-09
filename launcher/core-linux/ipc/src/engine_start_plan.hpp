#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "systemd_unit_state.hpp"

namespace rb::os_linux::ipc {

// The argv lists SystemdEngineStarter runs; argv[0] is looked up on PATH.

// systemctl --user show --property=LoadState,ActiveState,Listen <unit>.
[[nodiscard]] std::vector<std::string> show_unit_argv(std::string_view unit);

enum class SocketUnitAction { Skip, Start, Restart };

// Skip unless the socket unit is loaded and listens on exactly `socket_path`. Restart when it is
// active but nothing sits at the path: a foreground engine bound its own there and unlinked it.
[[nodiscard]] SocketUnitAction socket_unit_action(const SystemdUnitState& state, const NativePath& socket_path,
                                                  bool socket_present);

// systemctl --user start|restart reboot-engine.socket; `action` is not Skip.
[[nodiscard]] std::vector<std::string> socket_unit_argv(SocketUnitAction action);

// reboot-engine-<hash16>: one transient engine per data root.
[[nodiscard]] std::string transient_unit_name(std::string_view root_hash16);

// `<appimage> engine run --origin=on-demand` from an AppImage, whose mount behind `engine_exe`
// goes away with the client; `<engine_exe> run --origin=on-demand` otherwise.
[[nodiscard]] std::vector<std::string> engine_command(const NativePath& engine_exe,
                                                      const std::optional<NativePath>& appimage);

// systemd-run --user --unit=<unit> --collect --quiet --setenv=<each pinned> -- <command...>.
[[nodiscard]] std::vector<std::string> systemd_run_argv(std::string_view unit, std::span<const std::string> pinned,
                                                        std::span<const std::string> command);

}  // namespace rb::os_linux::ipc
