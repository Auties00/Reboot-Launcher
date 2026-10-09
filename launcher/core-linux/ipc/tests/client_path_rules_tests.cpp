#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "client_path_rules.hpp"

using rb::NativePath;
using rb::os_linux::ipc::classify_install;
using rb::os_linux::ipc::InstallFacts;
using rb::os_linux::ipc::tarball_root_of;
using rb::os_linux::ipc::xdg_home;
using rb::ports::InstallKind;

TEST_CASE("an XDG home counts only when absolute", "[client_paths]") {
    const NativePath fallback{"/home/ada/.local/share"};
    CHECK(xdg_home("/data", fallback) == NativePath{"/data"});
    CHECK(xdg_home("data", fallback) == fallback);
    CHECK(xdg_home("", fallback) == fallback);
    CHECK(xdg_home(std::nullopt, fallback) == fallback);
    CHECK(xdg_home("/srv/./x/../data", fallback).string() == "/srv/data");
    CHECK(xdg_home(std::nullopt, NativePath{"/home/ada/./.cache"}).string() == "/home/ada/.cache");
}

TEST_CASE("the tarball root is two levels above versions/<v>", "[client_paths]") {
    CHECK(tarball_root_of(NativePath{"/opt/rl/versions/1.2.3"}) == NativePath{"/opt/rl"});
    CHECK_FALSE(tarball_root_of(NativePath{"/opt/rl/current"}));
    CHECK_FALSE(tarball_root_of(NativePath{"versions/1.2.3"}));
    CHECK_FALSE(tarball_root_of(NativePath{"/opt/rl/versions/"}));
}

TEST_CASE("AppImage needs both a regular $APPIMAGE and the exe under $APPDIR", "[client_paths]") {
    InstallFacts facts{.exe_dir = NativePath{"/tmp/.mount_rl/usr/bin"},
                       .appimage = NativePath{"/home/ada/rl.AppImage"},
                       .appdir = "/tmp/.mount_rl"};
    CHECK(classify_install(facts) == InstallKind::AppImage);

    facts.appdir = "/tmp/.mount_other";
    CHECK(classify_install(facts) == InstallKind::Dev);
    facts.appdir = "tmp/.mount_rl";
    CHECK(classify_install(facts) == InstallKind::Dev);
    facts.appdir = std::nullopt;
    CHECK(classify_install(facts) == InstallKind::Dev);

    facts.appdir = "/tmp/.mount_rl";
    facts.appimage = std::nullopt;
    CHECK(classify_install(facts) == InstallKind::Dev);
}

TEST_CASE("Tarball needs versions/<v> beside a current symlink", "[client_paths]") {
    InstallFacts facts{.exe_dir = NativePath{"/opt/rl/versions/1.2.3"}, .tarball_current_is_link = true};
    CHECK(classify_install(facts) == InstallKind::Tarball);
    facts.tarball_current_is_link = false;
    CHECK(classify_install(facts) == InstallKind::Dev);
    facts = {.exe_dir = NativePath{"/home/ada/build/bin"}, .tarball_current_is_link = true};
    CHECK(classify_install(facts) == InstallKind::Dev);
}
