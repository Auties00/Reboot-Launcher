#include "messages.hpp"

namespace reboot::process::msg {

REBOOT_MESSAGE(kEnvInvalidName, "process.env_invalid_name",
               "The environment variable name {name} from the {layer} layer is not valid");
REBOOT_MESSAGE(kEnvInvalidValue, "process.env_invalid_value",
               "The value of environment variable {name} contains a NUL or invalid UTF-8");
REBOOT_MESSAGE(kEnvChannelName, "process.env_channel_name",
               "{name} is not a channel variable this launch may carry");
REBOOT_MESSAGE(kSpecRelativePath, "process.spec_relative_path", "{path} must be an absolute path");
REBOOT_MESSAGE(kSpecEmptyArgument, "process.spec_empty_argument", "Argument {index} of {program} is empty");
REBOOT_MESSAGE(kSpecInvalidArgument, "process.spec_invalid_argument",
               "Argument {index} of {program} contains a NUL or invalid UTF-8");
REBOOT_MESSAGE(kSpecNeedsControlChannel, "process.spec_needs_control_channel",
               "{program} must run with the control channel on its standard streams");
REBOOT_MESSAGE(kChildAlreadyStarted, "process.child_already_started", "{program} is already running");
REBOOT_MESSAGE(kChildHelloExpected, "process.child_hello_expected",
               "{program} sent frame {frame_type} before its Hello");
REBOOT_MESSAGE(kChildProtocolMismatch, "process.child_protocol_mismatch",
               "{program} speaks protocol {actual}, but this launcher needs protocol {expected}");
REBOOT_MESSAGE(kChildHelloTimeout, "process.child_hello_timeout", "{program} sent no Hello within {timeout}");
REBOOT_MESSAGE(kChildFrameTooLarge, "process.child_frame_too_large",
               "{program} sent a control frame larger than {limit} bytes");
REBOOT_MESSAGE(kChildMalformedOutput, "process.child_malformed_output",
               "{program} wrote output that is not a valid control frame");
REBOOT_MESSAGE(kChildUnresponsive, "process.child_unresponsive", "{program} did not answer {missed} liveness checks");
REBOOT_MESSAGE(kChildExited, "process.child_exited", "{program} exited unexpectedly with code {code}");
REBOOT_MESSAGE(kChildSignaled, "process.child_signaled", "{program} was ended by signal {signal}");
REBOOT_MESSAGE(kChildRestartLimit, "process.child_restart_limit",
               "{program} failed {restarts} times within {window} and is no longer restarted");
REBOOT_MESSAGE(kChildNotRunning, "process.child_not_running", "{program} is not running");
REBOOT_MESSAGE(kChildGone, "process.child_gone", "{program} exited before answering request {frame_type}");
REBOOT_MESSAGE(kChildRequestFailed, "process.child_request_failed", "{program} could not complete request {frame_type}");
REBOOT_MESSAGE(kChildRequestUnsupported, "process.child_request_unsupported",
               "{program} does not support request {frame_type}");
REBOOT_MESSAGE(kChildUnexpectedReply, "process.child_unexpected_reply",
               "{program} answered request {frame_type} with frame {reply_type}");
REBOOT_MESSAGE(kReapFailed, "process.reap_failed", "Cannot stop process {pid} left behind by the previous engine");
REBOOT_MESSAGE(kReapCancelled, "process.reap_cancelled",
               "Process {pid} left behind by the previous engine was not checked before startup was cancelled");

}  // namespace reboot::process::msg
