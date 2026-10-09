#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/os_linux/runner/umu_invocation.hpp"

using rb::NativePath;
using rb::os_linux::runner::UmuInvocation;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

namespace {

UmuInvocation invocation() {
    return UmuInvocation{NativePath{"/rt/umu/umu-run"}, NativePath{"/rt/ge-proton"}, NativePath{"/data/umu"}};
}

}  // namespace

TEST_CASE("a session launch pins the runtime and disables protonfixes", "[umu_invocation]") {
    CHECK(invocation().env() == EnvVars{{"PROTONPATH", "/rt/ge-proton"},
                                        {"GAMEID", "umu-default"},
                                        {"PROTONFIXES_DISABLE", "1"},
                                        {"UMU_FOLDERS_PATH", "/data/umu"},
                                        {"UMU_RUNTIME_UPDATE", "0"}});
}

TEST_CASE("the port layout runs umu-run from the GE-Proton root", "[umu_invocation]") {
    const rb::ports::RuntimeLayout layout = invocation().to_runtime_layout();
    CHECK(layout.root == NativePath{"/rt/ge-proton"});
    CHECK(layout.entry == NativePath{"/rt/umu/umu-run"});
    CHECK(layout.env == invocation().env());
    CHECK(layout.winhost_in_prefix_dir.empty());
}

TEST_CASE("only a layout that sets PROTONPATH is an umu layout", "[umu_invocation]") {
    CHECK(UmuInvocation::is_umu_layout(invocation().to_runtime_layout()));
    rb::ports::RuntimeLayout wine;
    wine.env = {{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}};
    CHECK_FALSE(UmuInvocation::is_umu_layout(wine));
}

TEST_CASE("exposed paths follow the inherited ones without duplicates", "[umu_invocation]") {
    const std::vector<NativePath> paths{NativePath{"/games/build"}, NativePath{"/data/winhost"}};
    const auto value = UmuInvocation::filesystems_rw("/games/build::/logs:", paths);
    REQUIRE(value);
    CHECK(*value == "/games/build:/logs:/data/winhost");
}

TEST_CASE("an empty inherited value exposes only the paths", "[umu_invocation]") {
    const std::vector<NativePath> paths{NativePath{"/data/winhost"}};
    const auto value = UmuInvocation::filesystems_rw("", paths);
    REQUIRE(value);
    CHECK(*value == "/data/winhost");
}

TEST_CASE("a path with a colon cannot be exposed", "[umu_invocation]") {
    const std::vector<NativePath> paths{NativePath{"/games/Season 1:2"}};
    const auto value = UmuInvocation::filesystems_rw("/logs", paths);
    REQUIRE_FALSE(value);
    CHECK(value.error().is(rb::os_linux::runner::kPathNotExposable));
}
