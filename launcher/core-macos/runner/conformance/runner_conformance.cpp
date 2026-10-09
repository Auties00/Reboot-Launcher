#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/xattr.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "messages.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_macos/runner/dxmt_wine_runner_platform.hpp"
#include "reboot/os_macos/runner/mac_runtime_layout.hpp"
#include "reboot/os_macos/runner/make_runner_platform.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"

namespace fs = std::filesystem;
using reboot::NativePath;
using reboot::os_macos::runner::DxmtWineRunnerPlatform;
using reboot::os_macos::runner::HostCpu;
using reboot::os_macos::runner::MacRuntimeLayout;
using reboot::ports::RunnerKind;

namespace {

constexpr const char* kQuarantine = "com.apple.quarantine";
constexpr std::string_view kQuarantineValue = "0081;00000000;Safari;";

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

void quarantine(const NativePath& path) {
    REQUIRE(::setxattr(path.c_str(), kQuarantine, kQuarantineValue.data(), kQuarantineValue.size(), 0, XATTR_NOFOLLOW) ==
            0);
}

// sysctl.proc_translated is 1 under Rosetta and missing on Intel.
bool runs_on_apple_silicon() {
#if defined(__arm64__)
    return true;
#else
    int translated = 0;
    std::size_t size = sizeof translated;
    return ::sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated == 1;
#endif
}

// Gives owner write back so the scratch dir can be removed.
struct RestoreWrite {
    NativePath dir;
    ~RestoreWrite() {
        std::error_code ignored;
        fs::permissions(dir, fs::perms::owner_write, fs::perm_options::add, ignored);
    }
};

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
    for (const NativePath& path : {dir.path(), lib_dir, dll}) quarantine(path);

    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    REQUIRE(platform.post_extract(dir.path()));
    CHECK_FALSE(quarantined(dir.path()));
    CHECK_FALSE(quarantined(lib_dir));
    CHECK_FALSE(quarantined(dll));
}

TEST_CASE("post_extract clears quarantine from read-only entries and keeps their modes", "[runner_conformance]") {
    const auto dir = scratch();
    write_runtime(dir.path());
    const NativePath ro_dir = dir.path() / "notices";
    const NativePath ro_file = ro_dir / "COPYING.LIB";
    touch(ro_file);
    quarantine(ro_file);
    quarantine(ro_dir);
    constexpr auto kFileMode = fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;
    constexpr auto kDirMode = kFileMode | fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    fs::permissions(ro_file, kFileMode);
    fs::permissions(ro_dir, kDirMode);
    const RestoreWrite restore{ro_dir};

    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    const auto stripped = platform.post_extract(dir.path());
    INFO((stripped ? std::string()
                   : stripped.error().id + " errno " +
                         std::to_string(stripped.error().os_error.value_or(reboot::SystemError{}).code)));
    REQUIRE(stripped);
    CHECK_FALSE(quarantined(ro_file));
    CHECK_FALSE(quarantined(ro_dir));
    CHECK(fs::symlink_status(ro_file).permissions() == kFileMode);
    CHECK(fs::symlink_status(ro_dir).permissions() == kDirMode);
}

TEST_CASE("post_extract leaves symlink targets outside the runtime alone", "[runner_conformance]") {
    const auto dir = scratch();
    const NativePath runtime = dir.path() / "runtime";
    write_runtime(runtime);
    const NativePath outside_file = dir.path() / "outside" / "file";
    const NativePath outside_nested = dir.path() / "outside" / "dir" / "nested";
    touch(outside_file);
    touch(outside_nested);
    quarantine(outside_file);
    quarantine(outside_nested);
    fs::create_symlink(outside_file, runtime / "file-link");
    fs::create_directory_symlink(outside_nested.parent_path(), runtime / "dir-link");

    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    REQUIRE(platform.post_extract(runtime));
    CHECK(quarantined(outside_file));
    CHECK(quarantined(outside_nested));
}

TEST_CASE("post_extract of a missing runtime fails", "[runner_conformance]") {
    const auto dir = scratch();
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    const auto stripped = platform.post_extract(dir.path() / "missing");
    REQUIRE_FALSE(stripped);
    CHECK(stripped.error().is(reboot::os_macos::runner::kQuarantineStripFailed));
    CHECK(stripped.error().kind == reboot::ErrorKind::NotFound);
    REQUIRE(stripped.error().os_error);
    CHECK(stripped.error().os_error->code == ENOENT);
}

TEST_CASE("an incomplete runtime fails the layout", "[runner_conformance]") {
    const auto dir = scratch();
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};
    const auto layout = platform.layout(RunnerKind::MacRuntime, {dir.path()});
    REQUIRE_FALSE(layout);
    CHECK(layout.error().is(reboot::os_macos::runner::kWineLoaderMissing));
    CHECK(layout.error().kind == reboot::ErrorKind::NotFound);
}

TEST_CASE("the platform passes the runner port suite", "[runner_conformance]") {
    const auto dir = scratch();
    const NativePath runtime = dir.path() / "runtime";
    write_runtime(runtime);
    quarantine(runtime / MacRuntimeLayout::kWineLoader);
    DxmtWineRunnerPlatform platform{HostCpu::AppleSilicon};

    const auto report = reboot::testing::run_runner_platform_conformance(
        platform,
        {RunnerKind::MacRuntime, {runtime}, dir.path() / "prefix", dir.path() / "winhost" / "reboot-winhost.exe"});
    INFO(report.describe());
    CHECK(report.passed());
    CHECK_FALSE(quarantined(runtime / MacRuntimeLayout::kWineLoader));
}

TEST_CASE("the factory detects the host CPU", "[runner_conformance]") {
    const auto platform = reboot::os_macos::runner::make_runner_platform();
    REQUIRE(platform);
    CHECK(platform->supported().empty() != runs_on_apple_silicon());
}
