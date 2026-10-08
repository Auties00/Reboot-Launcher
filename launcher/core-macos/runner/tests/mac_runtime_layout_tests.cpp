#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "reboot/os_macos/runner/mac_runtime_layout.hpp"

using reboot::NativePath;
using reboot::os_macos::runner::MacRuntimeLayout;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

TEST_CASE("the runner layer only disables winemenubuilder", "[mac_runtime_layout]") {
    const MacRuntimeLayout layout{NativePath{"/rt"}, NativePath{"/rt/bin/wine"}};
    CHECK(layout.env() == EnvVars{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}});
}

TEST_CASE("the port layout runs the wine loader with the runner layer", "[mac_runtime_layout]") {
    const MacRuntimeLayout layout{NativePath{"/rt"}, NativePath{"/rt/bin/wine"}};
    const reboot::ports::RuntimeLayout port = layout.to_runtime_layout();
    CHECK(port.root == NativePath{"/rt"});
    CHECK(port.entry == NativePath{"/rt/bin/wine"});
    CHECK(port.env == layout.env());
}
