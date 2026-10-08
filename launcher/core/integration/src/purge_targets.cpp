#include "reboot/integration/purge_targets.hpp"

#include "reboot/foundation/paths.hpp"

namespace reboot::integration {

namespace {

void append(std::vector<NativePath>& to, const std::vector<NativePath>& from) {
    to.insert(to.end(), from.begin(), from.end());
}

}  // namespace

PurgeTargets purge_targets(const AppLayout& layout) {
    PurgeTargets targets;
    targets.backend_data.push_back(layout.backend_dir());
    targets.logs.push_back(layout.logs_dir());
    // AppLayout names only the cache files, which all sit in its cache directory.
    targets.cache.push_back(layout.catalog_cache().parent_path());
    targets.components.push_back(layout.components_dir());
    targets.components.push_back(layout.prefixes_dir());
    // AppLayout names only each session's directory.
    targets.game_server_sessions.push_back(layout.root() / "data" / "game-server");
    return targets;
}

std::vector<NativePath> directories_for(const PurgeTargets& targets, PurgeScope scope) {
    switch (scope) {
        case PurgeScope::BackendData: return targets.backend_data;
        case PurgeScope::Logs: return targets.logs;
        case PurgeScope::Cache: return targets.cache;
        case PurgeScope::Components: return targets.components;
        case PurgeScope::All: break;
    }
    std::vector<NativePath> all;
    append(all, targets.backend_data);
    append(all, targets.logs);
    append(all, targets.cache);
    append(all, targets.components);
    append(all, targets.game_server_sessions);
    return all;
}

}  // namespace reboot::integration
