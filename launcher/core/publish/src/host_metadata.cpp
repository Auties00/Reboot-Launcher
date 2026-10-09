#include "reboot/publish/host_metadata.hpp"

#include <utility>

#include "field_checks.hpp"
#include "reboot/publish/field_limits.hpp"

namespace rb::publish {

Result<HostMetadata> fit_metadata(HostMetadata metadata) {
    auto name = fit_server_name(metadata.name);
    if (!name) return std::unexpected(std::move(name.error()));
    if (auto players = check_max_players(metadata.max_players); !players) return std::unexpected(std::move(players.error()));
    metadata.name = std::move(*name);
    metadata.description = fit_text(metadata.description, kMaxDescriptionBytes);
    metadata.author = fit_text(metadata.author, kMaxAuthorBytes);
    return metadata;
}

std::string author_for(const storage::AccountRecord& host) { return host.display_name; }

}  // namespace rb::publish
