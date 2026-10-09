#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/publish/listing.hpp"

namespace rb::publish {

// A live edit; only the fields set are sent.
struct MetadataPatch {
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<u32> max_players;
    std::optional<Listing> listing;
    // Committed by the UI on confirm, never per keystroke; empty removes the password.
    std::optional<SecretString> password;
};

// The fields set are fitted as fit_metadata fits them. Fails with publish.server_name_empty,
// publish.max_players_too_high, or publish.password_too_long above kMaxPasswordBytes.
[[nodiscard]] Result<MetadataPatch> fit_patch(MetadataPatch patch);

}  // namespace rb::publish
