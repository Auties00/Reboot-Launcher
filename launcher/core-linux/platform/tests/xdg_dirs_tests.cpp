#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "reboot/foundation/random.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "xdg_dirs.hpp"

namespace fs = std::filesystem;
using rb::NativePath;
using rb::ports::InstallKind;
using namespace rb::os_linux::platform;

namespace {

rb::testing::ScratchDir scratch() {
    rb::OsRandom random;
    auto dir = rb::testing::ScratchDir::create(random, "xdg-dirs");
    REQUIRE(dir);
    return std::move(*dir);
}

}  // namespace

TEST_CASE("an absolute XDG variable wins, normalised", "[xdg_dirs]") {
    CHECK(xdg_base("/srv/data//x/", "/home/ada", ".local/share") == NativePath{"/srv/data/x/"});
    CHECK(xdg_base("/srv/a/../b", "/home/ada", ".cache") == NativePath{"/srv/b"});
}

TEST_CASE("an unset, empty or relative XDG variable falls back to the home directory", "[xdg_dirs]") {
    CHECK(xdg_base(std::nullopt, "/home/ada", ".local/share") == NativePath{"/home/ada/.local/share"});
    CHECK(xdg_base("", "/home/ada", ".cache") == NativePath{"/home/ada/.cache"});
    CHECK(xdg_base("relative/dir", "/home/ada", ".local/state") == NativePath{"/home/ada/.local/state"});
}

TEST_CASE("the runtime base is XDG_RUNTIME_DIR only when absolute", "[xdg_dirs]") {
    CHECK(runtime_base_for("/run/user/1000", 1000) == NativePath{"/run/user/1000"});
    CHECK(runtime_base_for("run/user/1000", 1000) == NativePath{"/tmp/reboot-launcher-1000"});
    CHECK(runtime_base_for(std::nullopt, 42) == NativePath{"/tmp/reboot-launcher-42"});
}

TEST_CASE("a deleted executable's suffix is dropped", "[xdg_dirs]") {
    CHECK(without_deleted_suffix("/opt/x/reboot-engine (deleted)") == NativePath{"/opt/x/reboot-engine"});
    CHECK(without_deleted_suffix("/opt/x/reboot-engine") == NativePath{"/opt/x/reboot-engine"});
}

TEST_CASE("a versions/<v> directory beside a current link is a tarball install", "[xdg_dirs]") {
    const auto dir = scratch();
    const NativePath root = dir.path() / "app";
    fs::create_directories(root / "versions" / "11.0.0");
    fs::create_directory_symlink("versions/11.0.0", root / "current");

    const InstallFacts facts = detect_install(root / "versions" / "11.0.0", std::nullopt, std::nullopt);
    CHECK(facts.kind == InstallKind::Tarball);
    CHECK(facts.tarball_root == root);
    CHECK_FALSE(facts.appimage);
}

TEST_CASE("versions/<v> without a current link is a dev tree", "[xdg_dirs]") {
    const auto dir = scratch();
    fs::create_directories(dir.path() / "versions" / "11.0.0");
    CHECK(detect_install(dir.path() / "versions" / "11.0.0", std::nullopt, std::nullopt).kind == InstallKind::Dev);
    CHECK(detect_install(dir.path(), std::nullopt, std::nullopt).kind == InstallKind::Dev);
}

TEST_CASE("an AppImage needs a regular APPIMAGE file and the exe under APPDIR", "[xdg_dirs]") {
    const auto dir = scratch();
    const NativePath image = dir.path() / "Reboot.AppImage";
    std::ofstream{image} << "ELF";
    const NativePath mount = dir.path() / "mount";
    fs::create_directories(mount / "usr" / "bin");

    const InstallFacts facts = detect_install(mount / "usr" / "bin", image.native(), mount.native());
    CHECK(facts.kind == InstallKind::AppImage);
    CHECK(facts.appimage == image);

    CHECK(detect_install(dir.path() / "elsewhere", image.native(), mount.native()).kind == InstallKind::Dev);
    CHECK(detect_install(mount / "usr" / "bin", (dir.path() / "missing").native(), mount.native()).kind ==
          InstallKind::Dev);
    CHECK(detect_install(mount / "usr" / "bin", image.native(), std::nullopt).kind == InstallKind::Dev);
}
