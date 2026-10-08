#include "messages.hpp"

namespace reboot::net {

REBOOT_MESSAGE(kInvalidUrl, "net.invalid_url", "{url} is not a valid http or https address");
REBOOT_MESSAGE(kDnsFailed, "net.dns_failed", "Could not resolve {host}");
REBOOT_MESSAGE(kConnectFailed, "net.connect_failed", "Could not connect to {host}");
REBOOT_MESSAGE(kConnectTimeout, "net.connect_timeout", "{host} did not accept a connection within {limit}");
REBOOT_MESSAGE(kTlsFailed, "net.tls_failed", "The TLS certificate of {host} could not be verified");
REBOOT_MESSAGE(kRequestTimeout, "net.request_timeout", "{host} did not finish answering within {limit}");
REBOOT_MESSAGE(kTransferStalled, "net.transfer_stalled", "The transfer from {host} stalled for {limit}");
REBOOT_MESSAGE(kResponseTooLarge, "net.response_too_large", "{host} sent more than {byte_limit} bytes");
REBOOT_MESSAGE(kPlainHttpNeedsConsent, "net.plain_http_needs_consent",
               "{host} is reached over unencrypted http, which needs your confirmation");
REBOOT_MESSAGE(kHttpsDowngradeRefused, "net.https_downgrade_refused",
               "{host} was reached over https before, so unencrypted http to it is refused");
REBOOT_MESSAGE(kTransportFailed, "net.transport_failed", "The transfer from {host} failed");
REBOOT_MESSAGE(kRequestUnbounded, "net.request_unbounded",
               "A request to {host} has no connect timeout, or neither a total timeout nor a stall limit");

REBOOT_MESSAGE(kInsufficientSpace, "net.insufficient_space",
               "{file} needs {needed} bytes, but its volume has only {available} free");
REBOOT_MESSAGE(kRangeNotHonored, "net.range_not_honored", "{host} did not resume the download at byte {offset}");
REBOOT_MESSAGE(kDownloadSourceChanged, "net.download_source_changed",
               "The file on {host} changed while it was being downloaded");
REBOOT_MESSAGE(kDownloadSizeMismatch, "net.download_size_mismatch",
               "{host} sent {received} bytes instead of {expected}");
REBOOT_MESSAGE(kDownloadHttpStatus, "net.download_http_status", "{host} answered the download with HTTP {status}");
REBOOT_MESSAGE(kDownloadWriteFailed, "net.download_write_failed", "Could not write the download to {file}");
REBOOT_MESSAGE(kDownloadAttemptsExhausted, "net.download_attempts_exhausted",
               "The download from {host} failed {attempts} times in a row");

REBOOT_MESSAGE(kAddressInvalid, "net.address_invalid",
               "{address} is not a valid host, host:port or [IPv6]:port address");
REBOOT_MESSAGE(kHostNotFound, "net.host_not_found", "{address} could not be found");
REBOOT_MESSAGE(kNoIpv4Address, "net.no_ipv4_address", "{address} has no IPv4 address");
REBOOT_MESSAGE(kResolveTimeout, "net.resolve_timeout", "Looking up {address} took longer than {limit}");
REBOOT_MESSAGE(kResolveFailed, "net.resolve_failed", "Could not look up {address}");

REBOOT_MESSAGE(kProbePolicyInvalid, "net.probe_policy_invalid",
               "A probe needs a non-zero port, at least one attempt and a timeout");

REBOOT_MESSAGE(kPortBusy, "net.port_busy", "{protocol} port {port} is already in use by {owner}");
REBOOT_MESSAGE(kPortHeldBySystem, "net.port_held_by_system",
               "{protocol} port {port} is held by the operating system, for example its HTTP service");
REBOOT_MESSAGE(kPortAccessDenied, "net.port_access_denied",
               "{protocol} port {port} is reserved by the system and cannot be used");
REBOOT_MESSAGE(kPortTestFailed, "net.port_test_failed", "Could not test whether {protocol} port {port} is free");
REBOOT_MESSAGE(kPortOwnerUnknown, "net.port_owner_unknown", "The owner of {protocol} port {port} could not be read");

REBOOT_MESSAGE(kNoGateway, "net.no_gateway", "No router answered UPnP or NAT-PMP, so ports must be forwarded by hand");
REBOOT_MESSAGE(kMappingRefused, "net.mapping_refused", "The router refused to forward {protocol} port {port}");
REBOOT_MESSAGE(kMappingRenewFailed, "net.mapping_renew_failed",
               "The router stopped forwarding {protocol} port {port}");
REBOOT_MESSAGE(kMappingBlockInvalid, "net.mapping_block_invalid",
               "The port block to forward is empty or already forwarded");

REBOOT_MESSAGE(kQuicUnavailable, "net.quic_unavailable", "QUIC could not be started on this computer");
REBOOT_MESSAGE(kQuicConnectFailed, "net.quic_connect_failed", "Could not open a QUIC connection to {host}");
REBOOT_MESSAGE(kQuicNoIpv4, "net.quic_no_ipv4", "This computer has no IPv4 connection, which hosting requires");
REBOOT_MESSAGE(kQuicUdpBlocked, "net.quic_udp_blocked", "UDP traffic to {host} appears to be blocked");

}  // namespace reboot::net
