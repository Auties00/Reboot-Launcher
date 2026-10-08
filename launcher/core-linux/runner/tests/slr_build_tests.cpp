#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "reboot/os_linux/runner/slr_build.hpp"

using reboot::os_linux::runner::SlrBuild;

TEST_CASE("GE-Proton's tool manifest names its runtime", "[slr_build]") {
    constexpr std::string_view kManifest =
        "\"manifest\"\n{\n  \"version\" \"2\"\n  \"require_tool_appid\" \"1628350\"\n}\n";
    CHECK(SlrBuild::runtime_for(kManifest) == std::optional<std::string_view>{"steamrt3"});
}

TEST_CASE("an unknown or absent tool appid names no runtime", "[slr_build]") {
    CHECK_FALSE(SlrBuild::runtime_for("\"manifest\" { \"require_tool_appid\" \"999\" }"));
    CHECK_FALSE(SlrBuild::runtime_for("\"manifest\" { \"version\" \"2\" }"));
    CHECK_FALSE(SlrBuild::runtime_for("\"require_tool_appid\" \"1628350"));
}

TEST_CASE("the depot line gives the build", "[slr_build]") {
    constexpr std::string_view kVersions =
        "#Name\tVersion\tRuntime\tRuntime_Version\tComment\r\n"
        "depot\t0.20240806.99006\t-\t-\t# Overall version number of this depot\r\n"
        "sniper\t0.20240806.99006\tsniper\t0.20240806.99006\t# sniper_platform_0.20240806.99006\r\n";
    CHECK(SlrBuild::depot_version(kVersions) == std::optional<std::string>{"0.20240806.99006"});
}

TEST_CASE("VERSIONS.txt without a depot line gives no build", "[slr_build]") {
    CHECK_FALSE(SlrBuild::depot_version("#depot\t1\nsniper\t0.1\tsniper\n"));
    CHECK_FALSE(SlrBuild::depot_version("depot\t\t-\n"));
    CHECK_FALSE(SlrBuild::depot_version(""));
}
