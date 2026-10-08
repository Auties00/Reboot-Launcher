#include "messages.hpp"

namespace reboot::publish::msg {

REBOOT_MESSAGE(kNotPublished, "publish.not_published", "Session {session} is not published to the server browser");
REBOOT_MESSAGE(kAlreadyPublished, "publish.already_published",
               "Session {session} is already published to the server browser");
REBOOT_MESSAGE(kProfileBusy, "publish.profile_busy",
               "Host profile {profile} is already published by another running server");
REBOOT_MESSAGE(kNotRegistered, "publish.not_registered",
               "The server browser has not registered session {session} yet");

REBOOT_MESSAGE(kServerNameEmpty, "publish.server_name_empty", "The server name is empty");
REBOOT_MESSAGE(kMaxPlayersTooHigh, "publish.max_players_too_high", "A server can have at most {max} players");
REBOOT_MESSAGE(kPlayerCountTooHigh, "publish.player_count_too_high",
               "The server reported more than {max} players");
REBOOT_MESSAGE(kPasswordTooLong, "publish.password_too_long", "The join password can be at most {max} bytes long");
REBOOT_MESSAGE(kGamePortMissing, "publish.game_port_missing", "The game server has no port to publish");

REBOOT_MESSAGE(kEdgeUnreachable, "publish.edge_unreachable", "Could not reach the server browser at {host}");
REBOOT_MESSAGE(kConnectionLost, "publish.connection_lost", "Lost the connection to the server browser at {host}");
REBOOT_MESSAGE(kEdgeGoAway, "publish.edge_go_away", "The server browser asked to reconnect in {delay}");
REBOOT_MESSAGE(kEdgeRateLimited, "publish.edge_rate_limited", "The server browser asked to wait {delay}");
REBOOT_MESSAGE(kEdgeUnavailable, "publish.edge_unavailable", "The server browser is not ready yet");
REBOOT_MESSAGE(kEdgeRejected, "publish.edge_rejected", "The server browser rejected the {field} field");
REBOOT_MESSAGE(kEdgeHostLimit, "publish.edge_host_limit",
               "The server browser already lists the most servers allowed from this public address");
REBOOT_MESSAGE(kEdgeNotAllowed, "publish.edge_not_allowed",
               "The server browser refused a request this launcher is not allowed to make");

REBOOT_MESSAGE(kIdentityRotated, "publish.identity_rotated",
               "The server browser refused server id {old_server}, so the server now uses {new_server} and its "
               "share link changed");
REBOOT_MESSAGE(kHostedElsewhere, "publish.hosted_elsewhere",
               "Server id {server} was registered from another computer, so this server stopped publishing it");
REBOOT_MESSAGE(kTokenNotSaved, "publish.token_not_saved",
               "The ownership token of server id {server} could not be saved; if it is lost, the id is locked "
               "for 30 days");

REBOOT_MESSAGE(kIdentityDirUnavailable, "publish.identity_dir_unavailable",
               "The server identity folder {path} could not be created");
REBOOT_MESSAGE(kIdentityInUse, "publish.identity_in_use",
               "The server identity of host profile {profile} is used by a running server");
REBOOT_MESSAGE(kIdentityNotRegistered, "publish.identity_not_registered",
               "Host profile {profile} has no registered server id to export yet");
REBOOT_MESSAGE(kExportDestinationExists, "publish.export_destination_exists", "{path} already exists");
REBOOT_MESSAGE(kIdentityFileInvalid, "publish.identity_file_invalid", "{path} is not a host identity export");
REBOOT_MESSAGE(kIdentityUnreadable, "publish.identity_unreadable",
               "The server identity of host profile {profile} could not be read");

}  // namespace reboot::publish::msg
