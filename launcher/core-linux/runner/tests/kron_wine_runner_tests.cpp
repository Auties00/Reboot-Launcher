#include <catch2/catch_test_macros.hpp>

#include "reboot/os_linux/runner/kron_wine_runner.hpp"

using rb::NativePath;
using rb::os_linux::runner::KronWineRunner;

TEST_CASE("the Wine port layout runs the wine loader with no variables of its own", "[kron_wine_runner]") {
    const KronWineRunner runner{NativePath{"/rt"}, NativePath{"/rt/bin/wine"}};
    const rb::ports::RuntimeLayout layout = runner.to_runtime_layout();
    CHECK(layout.root == NativePath{"/rt"});
    CHECK(layout.entry == NativePath{"/rt/bin/wine"});
    CHECK(layout.env.empty());
}
