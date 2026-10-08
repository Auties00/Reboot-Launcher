#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/publish/host_metadata.hpp"
#include "reboot/publish/listing.hpp"

namespace reboot::publish {

// Sent by host once the session is ready and its block is mapped (or mapping failed). Move-only
// because of the password.
struct PublishRequest {
    SessionId session;
    HostProfileId profile;
    HostMetadata metadata;
    std::optional<SecretString> password;
    // From the session's pinned profile; the linked auto-server is always Unlisted.
    Listing listing = Listing::Unlisted;
    // The granted external port of the game socket, or its bound port.
    Port game_port;
    u32 players = 0;
};

// fit_metadata, then every other field the edge validates. Fails with publish.password_too_long,
// publish.game_port_missing for port 0, or publish.player_count_too_high above kMaxPlayerLimit.
[[nodiscard]] Result<PublishRequest> fit_request(PublishRequest request);

}  // namespace reboot::publish
