#pragma once

#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/integration/purge_scope.hpp"

namespace rb {
class AppLayout;
}

namespace rb::integration {

struct PurgeTargets {
    std::vector<NativePath> backend_data;
    std::vector<NativePath> logs;
    std::vector<NativePath> cache;
    // data/prefixes is rebuilt from the components, so it goes with them.
    std::vector<NativePath> components;
    // Only All reaches data/game-server.
    std::vector<NativePath> game_server_sessions;
};

[[nodiscard]] PurgeTargets purge_targets(const AppLayout& layout);

[[nodiscard]] std::vector<NativePath> directories_for(const PurgeTargets& targets, PurgeScope scope);

}  // namespace rb::integration
