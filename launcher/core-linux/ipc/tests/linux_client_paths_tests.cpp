#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "linux_ipc_test_support.hpp"
#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"
#include "reboot/os_linux/ipc/linux_client_paths.hpp"
#include "reboot/os_linux/ipc/make_client_platform.hpp"
#include "reboot/testing/port_conformance.hpp"

using namespace rb;
using namespace rb::os_linux::ipc;
using namespace rb::os_linux::ipc::test;

namespace {

[[nodiscard]] LinuxClientPaths detect_paths() {
    Result<LinuxClientPaths> paths = LinuxClientPaths::detect();
    REQUIRE(paths);
    return std::move(*paths);
}

// This test executable links the package statically, so it is the image holding the code.
[[nodiscard]] NativePath test_executable_dir() { return std::filesystem::canonical("/proc/self/exe").parent_path(); }

}  // namespace

TEST_CASE("detect takes the passwd home, absolute XDG homes and the image's directory", "[client_paths]") {
    const EnvOverride home{"HOME", "/reboot-launcher-not-the-home"};
    const EnvOverride data{"XDG_DATA_HOME", "/srv/data"};
    const EnvOverride cache{"XDG_CACHE_HOME", "relative/cache"};
    const EnvOverride state{"XDG_STATE_HOME", std::nullopt};
    const EnvOverride runtime{"XDG_RUNTIME_DIR", "/run/user/test"};
    const EnvOverride appimage{"APPIMAGE", std::nullopt};

    const LinuxClientPaths paths = detect_paths();
    CHECK(paths.home() != NativePath{"/reboot-launcher-not-the-home"});
    CHECK(paths.home().is_absolute());
    CHECK(paths.uid() == own_uid());
    CHECK_FALSE(paths.user_name().empty());
    CHECK_FALSE(paths.login_shell().empty());

    CHECK(paths.data_home() == NativePath{"/srv/data"});
    CHECK(paths.cache_home() == paths.home() / ".cache");
    CHECK(paths.state_home() == paths.home() / ".local" / "state");
    CHECK(paths.default_data_root() == NativePath{"/srv/data/reboot-launcher"});
    CHECK(paths.default_cache_root() == paths.home() / ".cache" / "reboot-launcher");
    CHECK(paths.default_logs_root() == paths.home() / ".local" / "state" / "reboot-launcher" / "logs");

    CHECK(paths.ipc_runtime_base() == NativePath{"/run/user/test"});
    CHECK(paths.runtime_base().from_xdg_runtime_dir);
    CHECK(paths.exe_dir() == test_executable_dir());
    CHECK(paths.install_kind() == ports::InstallKind::Dev);
    CHECK_FALSE(paths.appimage());
    CHECK_FALSE(paths.velopack_package_dir());
}

TEST_CASE("without an absolute XDG_RUNTIME_DIR the runtime base is the /tmp fallback", "[client_paths]") {
    const std::string expected = "/tmp/reboot-launcher-" + std::to_string(own_uid());
    {
        const EnvOverride runtime{"XDG_RUNTIME_DIR", std::nullopt};
        const LinuxClientPaths paths = detect_paths();
        CHECK(paths.ipc_runtime_base() == NativePath{expected});
        CHECK_FALSE(paths.runtime_base().from_xdg_runtime_dir);
        CHECK(linux_ipc_runtime_base(own_uid()).path == NativePath{expected});
    }
    const EnvOverride runtime{"XDG_RUNTIME_DIR", "run/user/1000"};
    CHECK(detect_paths().ipc_runtime_base() == NativePath{expected});
}

TEST_CASE("an AppImage install needs a regular $APPIMAGE file and the image under $APPDIR", "[client_paths]") {
    const auto scratch = make_private_scratch("linux-paths");
    const NativePath file = scratch.path() / "rl.AppImage";
    write_text(file, "", 0700);

    SECTION("the image under $APPDIR") {
        const EnvOverride appimage{"APPIMAGE", file.string()};
        const EnvOverride appdir{"APPDIR", test_executable_dir().parent_path().string()};
        const LinuxClientPaths paths = detect_paths();
        CHECK(paths.install_kind() == ports::InstallKind::AppImage);
        CHECK(paths.appimage() == std::optional<NativePath>{file});
    }
    SECTION("the image elsewhere") {
        const EnvOverride appimage{"APPIMAGE", file.string()};
        const EnvOverride appdir{"APPDIR", scratch.path().string()};
        const LinuxClientPaths paths = detect_paths();
        CHECK(paths.install_kind() == ports::InstallKind::Dev);
        CHECK_FALSE(paths.appimage());
    }
    SECTION("$APPIMAGE naming a directory") {
        const EnvOverride appimage{"APPIMAGE", scratch.path().string()};
        const EnvOverride appdir{"APPDIR", "/"};
        CHECK(detect_paths().install_kind() == ports::InstallKind::Dev);
    }
}

TEST_CASE("LinuxClientPaths pass the platform paths conformance suite", "[client_paths]") {
    const EnvOverride appimage{"APPIMAGE", std::nullopt};
    const LinuxClientPaths paths = detect_paths();
    const testing::ConformanceReport report = testing::run_platform_paths_conformance(paths);
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("make_client_platform composes every client port from one detection", "[client_paths]") {
    const EnvOverride runtime{"XDG_RUNTIME_DIR", std::nullopt};
    Result<ports::ClientPlatform> platform = ports::make_client_platform();
    REQUIRE(platform);
    REQUIRE(platform->connector);
    REQUIRE(platform->starter);
    REQUIRE(platform->caller);
    REQUIRE(platform->paths);
    REQUIRE(platform->revisions);
    CHECK(platform->self.user_id == std::to_string(own_uid()));
    CHECK(platform->self.pid != 0);
    CHECK(platform->paths->ipc_runtime_base() == linux_ipc_runtime_base(own_uid()).path);
    CHECK(platform->paths->exe_dir() == test_executable_dir());
}
