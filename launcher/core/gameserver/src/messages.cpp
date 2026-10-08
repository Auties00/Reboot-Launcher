#include "messages.hpp"

namespace reboot::gameserver::msg {

REBOOT_MESSAGE(kPathNotAbsolute, "gameserver.path_not_absolute", "{path} is not an absolute path");
REBOOT_MESSAGE(kExeUnreadable, "gameserver.exe_unreadable", "Cannot read the game server program {path}");
REBOOT_MESSAGE(kDescribeSpawnFailed, "gameserver.describe_spawn_failed", "Cannot start {path} to read its description");
REBOOT_MESSAGE(kDescribeTimeout, "gameserver.describe_timeout", "{path} did not describe itself within {timeout}");
REBOOT_MESSAGE(kDescribeNoOutput, "gameserver.describe_no_output", "{path} exited without describing itself");
REBOOT_MESSAGE(kDescribeMalformed, "gameserver.describe_malformed",
               "{path} wrote a description this launcher cannot read");
REBOOT_MESSAGE(kProtocolMismatch, "gameserver.protocol_mismatch",
               "{path} speaks game server protocol {actual}, but this launcher needs protocol {expected}");
REBOOT_MESSAGE(kInvalidSockets, "gameserver.invalid_sockets",
               "{path} declares {actual} game sockets, but it must declare exactly one");
REBOOT_MESSAGE(kDescriptionMismatch, "gameserver.description_mismatch",
               "The running game server does not match the description of {path}");
REBOOT_MESSAGE(kPortCountMismatch, "gameserver.port_count_mismatch",
               "The game server declares {expected} sockets but was given {actual} ports");
REBOOT_MESSAGE(kInvalidPort, "gameserver.invalid_port", "Port {port} is zero or appears twice in the port block");
REBOOT_MESSAGE(kBindNotIpv4, "gameserver.bind_not_ipv4", "The game server can only listen on IPv4, not on {address}");
REBOOT_MESSAGE(kBackendRequired, "gameserver.backend_required", "This game server needs a backend, but none was given");
REBOOT_MESSAGE(kInvalidMatchSetting, "gameserver.invalid_match_setting", "The match setting {field} is out of range");
REBOOT_MESSAGE(kInvalidAddress, "gameserver.invalid_address", "{address} is not an IP address or CIDR block");
REBOOT_MESSAGE(kSessionDirFailed, "gameserver.session_dir_failed", "Cannot create the game server folder {path}");
REBOOT_MESSAGE(kAlreadyStarted, "gameserver.already_started", "The game server of this session was already started");
REBOOT_MESSAGE(kNotRunning, "gameserver.not_running", "The game server of this session is not running");
REBOOT_MESSAGE(kStoppedBeforeStart, "gameserver.stopped_before_start", "The game server was stopped before it started");
REBOOT_MESSAGE(kCommandNotDeclared, "gameserver.command_not_declared",
               "This game server does not support the {command} command");
REBOOT_MESSAGE(kCommandTimeout, "gameserver.command_timeout",
               "The game server did not answer the {command} command within {timeout}");

}  // namespace reboot::gameserver::msg
