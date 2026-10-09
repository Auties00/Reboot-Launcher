#include "messages.hpp"

namespace reboot::play::msg {

REBOOT_MESSAGE(kNoBuildSelected, "play.no_build_selected", "Choose a build to play");
REBOOT_MESSAGE(kBuildVersionUnknown, "play.build_version_unknown", "Set the game version of {build} before playing");
REBOOT_MESSAGE(kSessionRunning, "play.session_running", "Close the running game before starting another one");
REBOOT_MESSAGE(kWrongSession, "play.wrong_session",
               "Start the game from the desktop session the launcher engine runs in");
REBOOT_MESSAGE(kNoDisplay, "play.no_display", "No display is available to show the game");
REBOOT_MESSAGE(kCustomArgsUnbalancedQuote, "play.custom_args_unbalanced_quote",
               "The custom launch arguments have an unclosed quote");
REBOOT_MESSAGE(kCustomArgsInvalid, "play.custom_args_invalid", "The custom launch arguments contain invalid text");
REBOOT_MESSAGE(kCustomArgsReserved, "play.custom_args_reserved",
               "The launcher sets {key} itself; remove it from the custom launch arguments");
REBOOT_MESSAGE(kUntestedDeclined, "play.untested_declined", "The launch was cancelled because this build is untested");
REBOOT_MESSAGE(kAutoServerDeclined, "play.auto_server_declined",
               "The launch was cancelled because the local game server was declined");
REBOOT_MESSAGE(kExitedBeforeLogin, "play.exited_before_login",
               "The game closed before logging in. The build may be damaged");
REBOOT_MESSAGE(kCrashed, "play.crashed", "The game crashed after being launched");
REBOOT_MESSAGE(kHookFailed, "play.hook_failed", "The client DLL could not apply {step} to this build");
REBOOT_MESSAGE(kFatal, "play.fatal", "The client DLL failed at {step}");
REBOOT_MESSAGE(kCorruptBuild, "play.corrupt_build", "The game reported that its files are damaged");
REBOOT_MESSAGE(kAuthFailure, "play.auth_failure", "The game could not log in");
REBOOT_MESSAGE(kCannotConnect, "play.cannot_connect", "The game could not reach the backend");
REBOOT_MESSAGE(kFeaturesDegraded, "play.features_degraded", "Some launcher features did not load: {features}");
REBOOT_MESSAGE(kLinkedServerEnded, "play.linked_server_ended", "The local game server stopped");
REBOOT_MESSAGE(kLinkedServerFailed, "play.linked_server_failed", "The local game server for this game did not start");
REBOOT_MESSAGE(kLaunchTimedOut, "play.launch_timed_out", "The game took too long to start");
REBOOT_MESSAGE(kSessionStopping, "play.session_stopping", "The game session is already stopping");
REBOOT_MESSAGE(kInvalidAnswer, "play.invalid_answer", "The answer to this question must be yes or no");

}  // namespace reboot::play::msg
