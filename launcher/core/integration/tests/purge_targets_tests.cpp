#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "reboot/foundation/paths.hpp"
#include "reboot/integration/purge_targets.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;
using namespace reboot::integration;

namespace {

[[nodiscard]] bool contains(const std::vector<NativePath>& paths, const NativePath& path) {
    return std::find(paths.begin(), paths.end(), path) != paths.end();
}

}  // namespace

TEST_CASE("each scope maps to its own directories", "[integration][purge]") {
    const testing::FakePlatformPaths paths;
    const AppLayout layout(DataRoot{paths.default_data_root(), false}, paths);
    const PurgeTargets targets = purge_targets(layout);

    CHECK(directories_for(targets, PurgeScope::BackendData) == std::vector{layout.backend_dir()});
    CHECK(directories_for(targets, PurgeScope::Logs) == std::vector{layout.logs_dir()});
    CHECK(directories_for(targets, PurgeScope::Cache) == std::vector{layout.catalog_cache().parent_path()});
    CHECK(directories_for(targets, PurgeScope::Components) ==
          std::vector{layout.components_dir(), layout.prefixes_dir()});
}

TEST_CASE("All adds the game-server sessions and never the settings or library", "[integration][purge]") {
    const testing::FakePlatformPaths paths;
    const AppLayout layout(DataRoot{paths.default_data_root(), false}, paths);
    const std::vector<NativePath> all = directories_for(purge_targets(layout), PurgeScope::All);

    CHECK(all.size() == 6);
    CHECK(contains(all, layout.root() / "data" / "game-server"));
    for (const NativePath& kept : {layout.root(), layout.settings_file().parent_path(), layout.library_file(),
                                   layout.accounts_file(), layout.state_file().parent_path()})
        CHECK_FALSE(contains(all, kept));
}
