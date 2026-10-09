#pragma once

#include <string_view>

namespace rb::os_linux::ipc {

// Must equal the socket unit the Linux integration registrar writes (ListenStream=
// %t/reboot-launcher/<hash16>.sock, SocketMode=0600, DirectoryMode=0700); this package may not
// include core-linux/platform.
inline constexpr std::string_view kEngineSocketUnit = "reboot-engine.socket";
// systemd-run appends <hash16>, so one transient engine exists per data root.
inline constexpr std::string_view kTransientUnitPrefix = "reboot-engine-";

}  // namespace rb::os_linux::ipc
