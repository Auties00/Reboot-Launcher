#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::publish {

inline constexpr std::size_t kHostTokenSize = 32;

// The rbsb/1 ownership token. The edge returns it only on an id's first registration; without it
// the id cannot be registered again for 30 days.
using HostToken = Secret<std::array<u8, kHostTokenSize>>;

// One per host profile, the built-in auto profile included. Move-only because of the token.
struct HostIdentity {
    HostProfileId profile;
    // Always minted by HostIdentityStore.
    ServerId server;
    // Absent until the first HostRegistered, and again after a rotation.
    std::optional<HostToken> token;
};

}  // namespace reboot::publish
