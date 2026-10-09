#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>

#include "reboot/foundation/random.hpp"
#include "reboot/os_windows/ipc/windows_file_revision_reader.hpp"
#include "reboot/testing/scratch_dir.hpp"

using namespace rb;

TEST_CASE("WindowsFileRevisionReader reads size, mtime and file id without blocking writers", "[os_windows][ipc][files]") {
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-revision-conformance");
    REQUIRE(scratch);
    const NativePath marker = scratch->path() / "update-in-progress";
    os_windows::ipc::WindowsFileRevisionReader reader;

    auto missing = reader.revision(marker);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "platform.ipc_call_failed_on_path");

    // A writer keeps the file open throughout.
    std::ofstream writer(marker, std::ios::binary);
    writer << "12345";
    writer.flush();
    const auto before = std::chrono::system_clock::now();
    auto first = reader.revision(marker);
    REQUIRE(first);
    CHECK(first->size == 5);
    CHECK(first->file_id != 0);
    CHECK(first->mtime > before - std::chrono::minutes{5});
    CHECK(first->mtime < before + std::chrono::minutes{5});

    writer << "678";
    writer.flush();
    auto second = reader.revision(marker);
    REQUIRE(second);
    CHECK(second->size == 8);
    CHECK(second->file_id == first->file_id);
}
