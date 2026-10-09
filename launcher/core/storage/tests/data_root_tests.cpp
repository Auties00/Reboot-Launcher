#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/storage/data_root.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace rb;
using namespace rb::storage;

TEST_CASE("the data root is created owner-only and missing bundled files are listed", "[storage][data_root]") {
    testing::FakePlatformPaths paths;
    testing::InMemoryFileSystem fs;
    const AppLayout layout(DataRoot{paths.default_data_root(), false}, paths);
    InstallLayout install{.install_dir = paths.exe_dir(),
                          .backend_exe = paths.exe_dir() / "reboot-backend",
                          .game_server_exe = paths.exe_dir() / "reboot-game-server",
                          .backend_content_dir = paths.exe_dir() / "backend",
                          .bundled_catalog = paths.exe_dir() / "catalog.json",
                          .bundled_manifest = paths.exe_dir() / "manifest.json"};
    fs.write_text(install.bundled_catalog, "{}");

    const Result<DataRootReport> report = prepare_data_root(layout, install, paths, fs);
    REQUIRE(report);
    CHECK(report->mode == StorageMode::ReadWrite);
    CHECK(fs.owner_only(layout.frontend_dir()));
    CHECK(fs.owner_only(layout.state_file().parent_path()));
    CHECK(report->missing_install_files.size() == 4);
}

TEST_CASE("a data root inside the Velopack package is refused", "[storage][data_root]") {
    testing::FakePlatformPaths paths;
    paths.set_velopack_package_dir(paths.default_data_root().parent_path());
    testing::InMemoryFileSystem fs;
    const AppLayout layout(DataRoot{paths.default_data_root(), false}, paths);

    const Result<DataRootReport> report = prepare_data_root(layout, InstallLayout{}, paths, fs);
    REQUIRE_FALSE(report);
    CHECK(report.error().id == "storage.root_inside_package");
}

TEST_CASE("a data root that cannot be created makes the stores memory-only", "[storage][data_root]") {
    testing::FakePlatformPaths paths;
    testing::InMemoryFileSystem fs;
    const AppLayout layout(DataRoot{paths.default_data_root(), false}, paths);
    fs.faults().fail_next(testing::FsOperation::CreateDirsOwnerOnly,
                          make_diag(ErrorDomain::Storage, MessageId{"storage.write_failed"}).build());

    const Result<DataRootReport> report = prepare_data_root(layout, InstallLayout{}, paths, fs);
    REQUIRE(report);
    CHECK(report->mode == StorageMode::InMemory);
    REQUIRE(report->reason);
    CHECK(report->reason->id == "storage.root_not_writable");
}
