#include "messages.hpp"

namespace rb::os_linux::ipc {

// This package owns platform.ipc_*; core-linux/platform owns every other platform.* id on Linux.
REBOOT_MESSAGE(kEndpointOutsideRuntimeDir, "platform.ipc_endpoint_outside_runtime_dir",
               "The engine socket {path} is not inside {expected_dir}.");
REBOOT_MESSAGE(kInheritedSocketInvalid, "platform.ipc_inherited_socket_invalid",
               "systemd passed {count} sockets, or one that is not a listening AF_UNIX stream socket.");
REBOOT_MESSAGE(kInheritedSocketMismatch, "platform.ipc_inherited_socket_mismatch",
               "The socket systemd passed is bound to {inherited_path}, not to {path}.");
REBOOT_MESSAGE(kHomeUnavailable, "platform.ipc_home_unavailable", "The home directory of uid {uid} could not be read.");
REBOOT_MESSAGE(kImageUnresolved, "platform.ipc_image_unresolved",
               "The location of the Reboot Launcher client library could not be read.");
REBOOT_MESSAGE(kEngineSpawnFailed, "platform.ipc_engine_spawn_failed", "{path} could not be started.");

}  // namespace rb::os_linux::ipc
