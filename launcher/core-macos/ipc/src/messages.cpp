#include "messages.hpp"

namespace rb::os_macos::ipc {

REBOOT_MESSAGE(kUserTempDirUnavailable, "platform.user_temp_dir_unavailable",
               "The per-user temporary directory could not be read.");
REBOOT_MESSAGE(kEndpointOutsideUserTemp, "platform.endpoint_outside_user_temp",
               "The engine socket {path} is not inside {expected_dir}.");
REBOOT_MESSAGE(kCallerSessionUnreadable, "platform.caller_session_unreadable",
               "The login session of this process could not be read.");
REBOOT_MESSAGE(kAgentRegisterFailed, "platform.agent_register_failed",
               "The engine background item {label} could not be registered.");
REBOOT_MESSAGE(kAgentRegisterTimedOut, "platform.agent_register_timed_out",
               "The engine background item {label} was not registered within {deadline}.");
REBOOT_MESSAGE(kAgentKickstartFailed, "platform.agent_kickstart_failed",
               "launchd could not start the engine agent {label}.");
REBOOT_MESSAGE(kAgentKickstartTimedOut, "platform.agent_kickstart_timed_out",
               "launchd did not start the engine agent {label} within {deadline}.");
REBOOT_MESSAGE(kHomeUnavailable, "platform.ipc_home_unavailable", "The home directory of uid {uid} could not be read.");
REBOOT_MESSAGE(kImageUnresolved, "platform.ipc_image_unresolved",
               "The location of the Reboot Launcher client library could not be read.");

}  // namespace rb::os_macos::ipc
