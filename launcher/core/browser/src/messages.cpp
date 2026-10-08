#include "messages.hpp"

namespace reboot::browser {

REBOOT_MESSAGE(kInvalidEndpointOverride, "browser.invalid_endpoint_override",
               "{value} is not a valid server browser address");
REBOOT_MESSAGE(kOffline, "browser.offline", "This computer appears to be offline");
REBOOT_MESSAGE(kServiceDown, "browser.service_down", "The server browser at {host} is down");
REBOOT_MESSAGE(kEdgeUnreachable, "browser.edge_unreachable", "Could not connect to the server browser at {host}");

REBOOT_MESSAGE(kRequestInvalid, "browser.request_invalid", "The server browser rejected a request from this launcher");
REBOOT_MESSAGE(kRequestUnsupported, "browser.request_unsupported",
               "The server browser does not support this request; a launcher update may be needed");
REBOOT_MESSAGE(kEdgeInternalError, "browser.edge_internal_error", "The server browser had an internal error");
REBOOT_MESSAGE(kEdgeUnavailable, "browser.edge_unavailable", "The server browser is temporarily unavailable");
REBOOT_MESSAGE(kRateLimited, "browser.rate_limited",
               "Too many requests to the server browser; try again in {retry_after}");
REBOOT_MESSAGE(kNotConnected, "browser.not_connected", "The server browser is not connected yet");
REBOOT_MESSAGE(kConnectionLost, "browser.connection_lost", "The connection to the server browser was lost");
REBOOT_MESSAGE(kRequestTimeout, "browser.request_timeout", "The server browser did not answer within {limit}");
REBOOT_MESSAGE(kRequestCancelled, "browser.request_cancelled", "The request to the server browser was cancelled");

REBOOT_MESSAGE(kInvalidViewSpec, "browser.invalid_view_spec", "The server list filter is not valid: {field}");
REBOOT_MESSAGE(kTooManyViews, "browser.too_many_views", "Too many server lists are open at once");
REBOOT_MESSAGE(kSearchTextLength, "browser.search_text_length", "Search text must be {min} to {max} bytes long");

REBOOT_MESSAGE(kJoinOwnServer, "browser.join_own_server", "This is your own server");
REBOOT_MESSAGE(kServerNotFound, "browser.server_not_found", "This server no longer exists");
REBOOT_MESSAGE(kServerOffline, "browser.server_offline", "This server is offline");
REBOOT_MESSAGE(kServerUnreachable, "browser.server_unreachable", "This server cannot be reached from the internet");
REBOOT_MESSAGE(kWrongPassword, "browser.wrong_password", "The server password is wrong");
REBOOT_MESSAGE(kJoinVersionMismatch, "browser.join_version_mismatch",
               "This server runs version {version}, but the build to launch is {local_version}");
REBOOT_MESSAGE(kTooManyJoinAttempts, "browser.too_many_join_attempts",
               "Too many attempts to join this server; try again in {retry_after}");
REBOOT_MESSAGE(kUnsupportedAddressFamily, "browser.unsupported_address_family",
               "{address} is reachable only over IPv6, which the game cannot use");
REBOOT_MESSAGE(kJoinRefused, "browser.join_refused", "Joining the server was cancelled");

REBOOT_MESSAGE(kInvalidLink, "browser.invalid_link", "{link} is not a server link");
REBOOT_MESSAGE(kLinkNotFound, "browser.link_not_found", "This link leads to no server: the server is gone");

REBOOT_MESSAGE(kTargetUnreachable, "browser.target_unreachable",
               "{address} did not answer; the server may be offline or may not answer probes");

}  // namespace reboot::browser
