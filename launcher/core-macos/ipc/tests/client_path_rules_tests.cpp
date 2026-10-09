#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "client_path_rules.hpp"

using reboot::NativePath;
using reboot::os_macos::ipc::ImagePlacement;
using reboot::os_macos::ipc::place_image;

namespace {

void check_bundle(std::string_view image, std::string_view bundle) {
    INFO(image);
    const ImagePlacement placement = place_image(NativePath{image});
    REQUIRE(placement.app_bundle);
    CHECK(*placement.app_bundle == NativePath{bundle});
    CHECK(placement.exe_dir == NativePath{bundle} / "Contents" / "MacOS");
}

void check_no_bundle(std::string_view image) {
    INFO(image);
    const ImagePlacement placement = place_image(NativePath{image});
    CHECK_FALSE(placement.app_bundle);
    CHECK(placement.exe_dir == NativePath{image}.parent_path());
}

}  // namespace

TEST_CASE("an image in Contents/MacOS belongs to its bundle", "[client_path_rules]") {
    check_bundle("/Applications/Reboot Launcher.app/Contents/MacOS/Reboot Launcher", "/Applications/Reboot Launcher.app");
    check_bundle("/Applications/Reboot Launcher.app/Contents/MacOS/reboot-cli", "/Applications/Reboot Launcher.app");
}

TEST_CASE("a library in Contents/Frameworks points at the bundle's MacOS directory", "[client_path_rules]") {
    check_bundle("/Applications/Reboot Launcher.app/Contents/Frameworks/libreboot_client.dylib",
                 "/Applications/Reboot Launcher.app");
    check_bundle("/Applications/Reboot Launcher.app/Contents/Frameworks/RebootClient.framework/Versions/A/RebootClient",
                 "/Applications/Reboot Launcher.app");
}

TEST_CASE("a nested helper app is its own bundle", "[client_path_rules]") {
    check_bundle("/Applications/Outer.app/Contents/Library/LoginItems/Helper.app/Contents/MacOS/helper",
                 "/Applications/Outer.app/Contents/Library/LoginItems/Helper.app");
}

TEST_CASE("a translocated bundle keeps its translocated path", "[client_path_rules]") {
    check_bundle("/private/var/folders/ab/cd/T/AppTranslocation/0A1B/d/Reboot Launcher.app/Contents/MacOS/Reboot Launcher",
                 "/private/var/folders/ab/cd/T/AppTranslocation/0A1B/d/Reboot Launcher.app");
}

TEST_CASE("an image outside the bundle's code directories is not in a bundle", "[client_path_rules]") {
    check_no_bundle("/Applications/Reboot Launcher.app/Contents/Resources/libreboot_client.dylib");
    check_no_bundle("/Applications/Reboot Launcher.app/libreboot_client.dylib");
    check_no_bundle("/Applications/Reboot Launcher.app/Contents/libreboot_client.dylib");
    // The innermost .app decides, even when an outer one would match.
    check_no_bundle("/Applications/Outer.app/Contents/MacOS/Inner.app/Resources/lib.dylib");
}

TEST_CASE("an image outside any bundle uses its own directory", "[client_path_rules]") {
    check_no_bundle("/usr/local/lib/libreboot_client.dylib");
    check_no_bundle("/Users/me/src/launcher/build/core/client/libreboot_client.dylib");
    check_no_bundle("/Users/me/.app/Contents/MacOS/reboot-cli");
    check_no_bundle("/reboot-cli");
}
