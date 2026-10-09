#include "reboot/publish/publish_request.hpp"

#include <utility>

#include "field_checks.hpp"

namespace rb::publish {

Result<PublishRequest> fit_request(PublishRequest request) {
    auto metadata = fit_metadata(std::move(request.metadata));
    if (!metadata) return std::unexpected(std::move(metadata.error()));
    request.metadata = std::move(*metadata);
    if (request.password) {
        if (auto password = check_password(*request.password); !password)
            return std::unexpected(std::move(password.error()));
    }
    if (auto port = check_game_port(request.game_port); !port) return std::unexpected(std::move(port.error()));
    if (auto players = check_player_count(request.players); !players) return std::unexpected(std::move(players.error()));
    return request;
}

}  // namespace rb::publish
