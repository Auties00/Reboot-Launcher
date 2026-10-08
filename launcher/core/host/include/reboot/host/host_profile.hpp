#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/gameserver/game_server_config.hpp"
#include "reboot/host/match_end_policy.hpp"
#include "reboot/host/operator_policy.hpp"
#include "reboot/host/port_policy.hpp"
#include "reboot/storage/settings_values.hpp"

namespace reboot::host {

using HostListing = storage::HostListing;
using HostUpdatePolicy = storage::HostUpdatePolicy;

// Fixed ids, so the CLI can name them.
inline constexpr HostProfileId kDefaultProfileId{
    Uuid{{0x7c, 0x1e, 0x4a, 0x52, 0x0d, 0x3b, 0x4f, 0x11, 0x9a, 0x01, 0x52, 0x45, 0x42, 0x4f, 0x4f, 0x54}}};
inline constexpr HostProfileId kAutoProfileId{
    Uuid{{0x7c, 0x1e, 0x4a, 0x52, 0x0d, 0x3b, 0x4f, 0x11, 0x9a, 0x02, 0x52, 0x45, 0x42, 0x4f, 0x4f, 0x54}}};

inline constexpr std::size_t kMaxProfileNameLength = 64;

// A version hosted with no installed build: our game server reads no game files, so the
// GameTarget it gets has no build_root.
struct HostVersion {
    GameVersion version;
    Changelist cl;

    bool operator==(const HostVersion&) const = default;
};

// One way to host. The join password is a secret keyed by the profile id and the rbsb identity
// {server_id, token} belongs to publish; neither is stored here.
struct HostProfile {
    HostProfileId id;
    // Bumped by every write of this profile; an update naming an older one is refused.
    u64 revision = 0;
    // Unique without regard to ASCII case.
    std::string name;
    // With `version` also absent: the library's host selection at start.
    std::optional<BuildId> build;
    // Never set together with `build`.
    std::optional<HostVersion> version;
    PortPolicy port = AutoPorts{};
    // UPnP or NAT-PMP for the whole block; a failure is only a diagnostic.
    bool port_mapping = true;
    HostListing listing = HostListing::Unlisted;
    // Empty: the localized default at publish time.
    std::string server_name;
    std::string description;
    gameserver::MatchSettings match;
    MatchEndPolicy match_end;
    OperatorPolicy operators;
    // Absent: the host.update_policy setting. A dedicated server profile is usually Manual.
    std::optional<HostUpdatePolicy> update_policy;

    [[nodiscard]] bool is_auto() const noexcept { return id == kAutoProfileId; }
    [[nodiscard]] bool is_builtin() const noexcept { return id == kAutoProfileId || id == kDefaultProfileId; }
};

// What a new profile starts with: Auto ports and the host.listing setting.
[[nodiscard]] HostProfile new_profile(HostProfileId id, std::string name, HostListing default_listing);

// The built-in profile of the linked auto-server: Auto ports, always Unlisted, and no port
// mapping, so starting a game never changes the router on its own (10.0.9 did map it).
[[nodiscard]] HostProfile auto_profile();

// What a Host reset leaves of a profile: Unlisted, and for a built-in profile also an empty server
// name and description and the default match-end policy. The id, name, ports, build, operators
// and the join password and rbsb identity, which live elsewhere, are kept.
[[nodiscard]] HostProfile reset_profile(HostProfile profile);

// Every rule a stored profile must meet: name (host.profile_name_empty, host.profile_name_too_long),
// not both a build and a version (host.build_and_version), server name and description within
// publish's rbsb/1 limits, the port, match-end and operator
// policies, and the auto profile staying Unlisted (host.auto_profile_listed). Returns the profile
// with its operator policy normalized.
[[nodiscard]] Result<HostProfile> validate(HostProfile profile);

}  // namespace reboot::host
