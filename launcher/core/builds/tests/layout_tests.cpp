#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <variant>

#include "builds_test_support.hpp"
#include "reboot/builds/file_finder.hpp"
#include "reboot/builds/layout_resolver.hpp"
#include "reboot/foundation/cancel.hpp"

using namespace rb;
using namespace rb::builds;
using namespace rb::builds::test;

namespace {

const NativePath kWin64 = NativePath("FortniteGame") / "Binaries" / "Win64";
const NativePath kEngineWin64 = NativePath("Engine") / "Binaries" / "Win64";

void touch(const NativePath& path) { write_text(path, "x"); }

BuildLayout resolved(const LayoutResolver& resolver, const NativePath& folder,
                     const std::optional<NativePath>& chosen = std::nullopt) {
    auto resolution = resolver.resolve(folder, chosen, CancelToken{});
    REQUIRE(resolution);
    const auto* layout = std::get_if<BuildLayout>(&*resolution);
    REQUIRE(layout);
    return *layout;
}

}  // namespace

TEST_CASE("the finder matches many names in one walk, ignoring ASCII case, in name order") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-find");
    touch(dir.path() / "b" / "FORTNITELAUNCHER.EXE");
    touch(dir.path() / "a" / "fortnitelauncher.exe");
    touch(dir.path() / "a" / "other.txt");
    touch(dir.path() / "CrashReportClient.exe");

    const std::array<std::string_view, 2> names{kLauncherExe, kCrashReportClientExe};
    auto found = FileFinder{}.find(dir.path(), names, CancelToken{});
    REQUIRE(found);
    REQUIRE(found->files.size() == 3);
    // Uppercase sorts before lowercase.
    CHECK(found->files[0] == FoundFile{.name_index = 1, .relative = NativePath("CrashReportClient.exe")});
    CHECK(found->files[1] == FoundFile{.name_index = 0, .relative = NativePath("a") / "fortnitelauncher.exe"});
    CHECK(found->files[2] == FoundFile{.name_index = 0, .relative = NativePath("b") / "FORTNITELAUNCHER.EXE"});
    CHECK(found->errors.empty());
    CHECK_FALSE(found->depth_capped);
}

TEST_CASE("the finder stops at its depth and when cancelled, and needs a readable root") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-depth");
    touch(dir.path() / "1" / "2" / "3" / std::string(kShippingExe));
    const std::array<std::string_view, 1> names{kShippingExe};

    auto shallow = FileFinder(FindOptions{.max_depth = 2}).find(dir.path(), names, CancelToken{});
    REQUIRE(shallow);
    CHECK(shallow->files.empty());
    CHECK(shallow->depth_capped);
    auto deep = FileFinder(FindOptions{.max_depth = 3}).find(dir.path(), names, CancelToken{});
    REQUIRE(deep);
    CHECK(deep->files.size() == 1);

    CancelSource cancel;
    cancel.cancel(CancelReason::User);
    auto cancelled = FileFinder{}.find(dir.path(), names, cancel.token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().id == "builds.cancelled");

    auto missing = FileFinder{}.find(dir.path() / "missing", names, CancelToken{});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "builds.io");
    auto file = FileFinder{}.find(dir.path() / "1" / "2" / "3" / std::string(kShippingExe), names, CancelToken{});
    REQUIRE_FALSE(file);
    CHECK(file.error().id == "builds.not_a_directory");
}

TEST_CASE("the finder never follows a directory link") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-link");
    touch(dir.path() / "real" / std::string(kShippingExe));
    std::error_code ec;
    std::filesystem::create_directory_symlink(dir.path() / "real", dir.path() / "loop", ec);
    if (ec) SKIP("creating a directory link needs a privilege this run lacks");
    const std::array<std::string_view, 1> names{kShippingExe};
    auto found = FileFinder{}.find(dir.path(), names, CancelToken{});
    REQUIRE(found);
    REQUIRE(found->files.size() == 1);
    CHECK(found->files[0].relative == NativePath("real") / std::string(kShippingExe));
}

TEST_CASE("a standard build resolves every file, relative to its root") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-layout");
    const NativePath root = dir.path() / "12.41";
    touch(root / kWin64 / std::string(kShippingExe));
    touch(root / kWin64 / std::string(kLauncherExe));
    touch(root / "Other" / std::string(kLauncherExe));
    touch(root / kWin64 / std::string(kEacExe));
    touch(root / "A" / std::string(kCrashReportClientExe));
    touch(root / kEngineWin64 / std::string(kCrashReportClientExe));
    touch(root / kWin64 / std::string(kAftermathDll));
    touch(root / "Engine" / "Plugins" / std::string(kAftermathDll));

    const LayoutResolver resolver;
    const BuildLayout layout = resolved(resolver, root);
    CHECK(layout.root == root);
    CHECK(layout.shipping_exe == kWin64 / std::string(kShippingExe));
    CHECK(layout.launcher_exe == kWin64 / std::string(kLauncherExe));
    CHECK(layout.eac_exe == kWin64 / std::string(kEacExe));
    REQUIRE(layout.crash_report_clients.size() == 2);
    CHECK(layout.crash_report_clients[0] == kEngineWin64 / std::string(kCrashReportClientExe));
    CHECK(layout.crash_report_clients[1] == NativePath("A") / std::string(kCrashReportClientExe));
    CHECK(layout.aftermath_dlls.size() == 2);
    CHECK(layout.binaries_dir() == root / kWin64);
    CHECK(resolver.still_valid(layout));

    std::filesystem::remove(root / kWin64 / std::string(kAftermathDll));
    CHECK_FALSE(resolver.still_valid(layout));
}

TEST_CASE("a wrapper folder or the picked Win64 folder settles the same root") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-wrapper");
    const NativePath root = dir.path() / "download" / "Fortnite 7.40";
    touch(root / kWin64 / std::string(kShippingExe));
    touch(root / kEngineWin64 / std::string(kCrashReportClientExe));

    const LayoutResolver resolver;
    CHECK(resolved(resolver, dir.path() / "download").root == root);
    const BuildLayout from_win64 = resolved(resolver, root / kWin64);
    CHECK(from_win64.root == root);
    CHECK(from_win64.crash_report_clients.size() == 1);
}

TEST_CASE("several shipping exes need a choice, given relative to the folder") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-choice");
    touch(dir.path() / "a" / kWin64 / std::string(kShippingExe));
    touch(dir.path() / "b" / kWin64 / std::string(kShippingExe));
    touch(dir.path() / "b" / kWin64 / std::string(kLauncherExe));

    const LayoutResolver resolver;
    auto resolution = resolver.resolve(dir.path(), std::nullopt, CancelToken{});
    REQUIRE(resolution);
    const auto* choice = std::get_if<NeedsShippingChoice>(&*resolution);
    REQUIRE(choice);
    CHECK(choice->folder == dir.path());
    REQUIRE(choice->candidates.size() == 2);
    CHECK(choice->candidates[0] == NativePath("a") / kWin64 / std::string(kShippingExe));

    const BuildLayout picked = resolved(resolver, dir.path(), choice->candidates[1]);
    CHECK(picked.root == dir.path() / "b");
    CHECK(picked.launcher_exe == kWin64 / std::string(kLauncherExe));

    auto unknown = resolver.resolve(dir.path(), NativePath("c") / std::string(kShippingExe), CancelToken{});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().id == "builds.shipping_not_found");
}

TEST_CASE("a folder without a shipping exe, or one that is no folder, fails") {
    const testing::ScratchDir dir = make_scratch("reboot-builds-missing");
    touch(dir.path() / "readme.txt");
    const LayoutResolver resolver;
    auto missing = resolver.resolve(dir.path(), std::nullopt, CancelToken{});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().id == "builds.missing_shipping");

    auto file = resolver.resolve(dir.path() / "readme.txt", std::nullopt, CancelToken{});
    REQUIRE_FALSE(file);
    CHECK(file.error().id == "builds.not_a_directory");
}
