#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/host/port_policy.hpp"

namespace reboot::host {

enum class HostErrorCode : u8 {
    ProfileNotFound,
    ProfileNameEmpty,
    ProfileNameTooLong,
    ProfileNameTaken,
    ServerNameTooLong,
    DescriptionTooLong,
    // An update that names a revision older than the stored one.
    ProfileStale,
    // The default and auto profiles cannot be deleted.
    BuiltinProfile,
    AutoProfileListed,
    InvalidPortPolicy,
    ReservedPort,
    InvalidMatchEndDelay,
    InvalidOperatorAddress,
    BanWithoutTarget,
    // One live session per profile: a second rbsb registration with the same token would
    // supersede the first.
    ProfileBusy,
    HostLimitReached,
    // linked_to set with a profile other than the auto profile.
    LinkedNeedsAutoProfile,
    // The auto profile started without linked_to.
    AutoProfileNeedsLink,
    NoBuildSelected,
    BuildVersionUnknown,
    // The block does not fit below 65536, or not inside the Auto range at all.
    BlockOutOfRange,
    NoFreeBlock,
    // No Listening within ReadinessPolicy::deadline of the spawn.
    ListenTimeout,
    // The server reported Listening, but another process holds one of its ports.
    PortNotOwned,
    ReadinessTimeout,
    NotHostSession,
    // A command for a session that is Restarting, Stopping or has no server process.
    ServerNotRunning,
    // listening() before the session's server reported Listening.
    NotListening,
    // A profile names both a build and a version to host.
    BuildAndVersion,
};

// Only the members a code's message names are read: profile, session, name, port, range
// (first, last), block_size, limit and address.
struct HostError {
    HostErrorCode code = HostErrorCode::ProfileNotFound;
    std::optional<HostProfileId> profile;
    std::optional<SessionId> session;
    std::string name;
    std::optional<Port> port;
    std::optional<PortRange> range;
    std::optional<u16> block_size;
    std::optional<u32> limit;
    // InvalidOperatorAddress: the entry that did not parse.
    std::string address;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const HostError& error);

}  // namespace reboot::host
