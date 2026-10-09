#pragma once

#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/accounts_document.hpp"

namespace reboot::publish {

// What the entry shows. The join password travels separately as a secret.
struct HostMetadata {
    std::string name;
    std::string description;
    // The host display name, never the login or an email.
    std::string author;
    GameVersion version;
    u32 max_players = 0;

    bool operator==(const HostMetadata&) const = default;
};

// sanitize_display_text on each field with tabs as spaces (the edge refuses control characters),
// then the name, description and author are cut on a code
// point boundary to their field_limits.hpp limits. Fails with
// publish.server_name_empty when nothing of the name is left, and publish.max_players_too_high
// above kMaxPlayerLimit.
[[nodiscard]] Result<HostMetadata> fit_metadata(HostMetadata metadata);

// The author shown for a host record: its display name, which is at most 16 [A-Za-z0-9] chars.
[[nodiscard]] std::string author_for(const storage::AccountRecord& host);

}  // namespace reboot::publish
