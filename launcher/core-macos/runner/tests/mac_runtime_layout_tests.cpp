#include <catch2/catch_test_macros.hpp>

#include "reboot/os_macos/runner/mac_runtime_layout.hpp"

using reboot::NativePath;
using reboot::os_macos::runner::MacRuntimeLayout;

TEST_CASE("the port layout runs the wine loader with no variables of its own", "[mac_runtime_layout]") {
    const MacRuntimeLayout layout{NativePath{"/rt"}, NativePath{"/rt/bin/wine"}};
    const reboot::ports::RuntimeLayout port = layout.to_runtime_layout();
    CHECK(port.root == NativePath{"/rt"});
    CHECK(port.entry == NativePath{"/rt/bin/wine"});
    CHECK(port.env.empty());
}
