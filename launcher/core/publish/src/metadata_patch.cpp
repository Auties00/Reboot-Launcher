#include "reboot/publish/metadata_patch.hpp"

#include <utility>

#include "field_checks.hpp"
#include "reboot/publish/field_limits.hpp"

namespace reboot::publish {

Result<MetadataPatch> fit_patch(MetadataPatch patch) {
    if (patch.name) {
        auto name = fit_server_name(*patch.name);
        if (!name) return std::unexpected(std::move(name.error()));
        patch.name = std::move(*name);
    }
    if (patch.description) patch.description = fit_text(*patch.description, kMaxDescriptionBytes);
    if (patch.max_players) {
        if (auto players = check_max_players(*patch.max_players); !players)
            return std::unexpected(std::move(players.error()));
    }
    if (patch.password) {
        if (auto password = check_password(*patch.password); !password)
            return std::unexpected(std::move(password.error()));
    }
    return patch;
}

}  // namespace reboot::publish
