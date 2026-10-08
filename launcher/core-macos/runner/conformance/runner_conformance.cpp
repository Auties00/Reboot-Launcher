#include <catch2/catch_test_macros.hpp>

#include <sys/xattr.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "messages.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_macos/runner/dxmt_wine_runner_platform.hpp"
#include "reboot/os_macos/runner/mac_runtime_layout.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace fs = std::filesystem;
using reboot::NativePath;
using reboot::os_macos::runner::MacRuntimeLayout;

namespace {

constexpr const char* kQuarantine = "com.apple.quarantine";

void touch(const NativePath& path) {
    fs::create_directories(path.parent_path());
    std::ofstream{path};
}

void write_runtime(const NativePath& root) {
    touch(root / MacRuntimeLayout::kWineLoader);
    fs::permissions(root / MacRuntimeLayout::kWineLoader, fs::perms::owner_exec, fs::perm_options::add);
    for (const std::string_view dll : MacRuntimeLayout::kDxmtDlls) touch(root / MacRuntimeLayout::kWindowsDllDir / dll);
    touch(root / MacRuntimeLayout::kUnixLibDir / MacRuntimeLayout::kDxmtUnixLib);
}

bool quarantined(const NativePath& path) {
    return ::getxattr(path.c_str(), kQuarantine, nullptr, 0, 0, XATTR_NOFOLLOW) >= 0;
}

reboot::testing::ScratchDir scratch() {
    reboot::OsRandom random;
    auto dir = reboot::testing::ScratchDir::create(random, "runner");
    REQUIRE(dir);
    return std::move(*dir);
}

}  // namespace

TEST_CASE("a complete runtime resolves", "[runner_conformance]") {
    const auto dir = scratch();
    write_runtime(dir.path());
    const auto layout = MacRuntimeLayout::resolve(dir.path());
    REQUIRE(layout);
    CHECK(layout->wine == dir.path() / MacRuntimeLayout::kWineLoader);
}

TEST_CASE("a non-executable wine loader is missing", "[runner_conformance]") {
    const auto dir = scratch();
    write_runtime(dir.path());
    fs::permissions(dir.path() / MacRuntimeLayout::kWineLoader, fs::perms::owner_read);
    const auto layout = MacRuntimeLayout::resolve(dir.path());
    REQUIRE_FALSE(layout);
    CHECK(layout.error().is(reboot::os_macos::runner::kWineLoaderMissing));
}

TEST_CASE("a missing DXMT file is named", "[runner_conformance]") {
    const auto dir = scratch();
    write_runtime(dir.path());
    fs::remove(dir.path() / MacRuntimeLayout::kWindowsDllDir / "dxgi.dll");
    const auto layout = MacRuntimeLayout::resolve(dir.path());
    REQUIRE_FALSE(layout);
    CHECK(layout.error().is(reboot::os_macos::runner::kDxmtMissing));
    const reboot::Arg* file = layout.error().find_arg("file");
    REQUIRE(file);
    CHECK(std::get<std::string>(*file) == "dxgi.dll");
}

TEST_CASE("post_extract clears quarantine from the whole tree", "[runner_conformance]") {
    const auto dir = scratch();
    write_runtime(dir.path());
    const NativePath dll = dir.path() / MacRuntimeLayout::kWindowsDllDir / "d3d11.dll";
    const NativePath lib_dir = dir.path() / MacRuntimeLayout::kUnixLibDir;
    constexpr std::string_view kValue = "0081;00000000;Safari;";
    for (const NativePath& path : {dir.path(), lib_dir, dll})
        REQUIRE(::setxattr(path.c_str(), kQuarantine, kValue.data(), kValue.size(), 0, XATTR_NOFOLLOW) == 0);

    reboot::os_macos::runner::DxmtWineRunnerPlatform platform{reboot::os_macos::runner::HostCpu::AppleSilicon};
    REQUIRE(platform.post_extract(dir.path()));
    CHECK_FALSE(quarantined(dir.path()));
    CHECK_FALSE(quarantined(lib_dir));
    CHECK_FALSE(quarantined(dll));
}
