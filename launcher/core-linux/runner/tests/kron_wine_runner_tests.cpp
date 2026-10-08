#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "reboot/os_linux/runner/kron_wine_runner.hpp"

using reboot::NativePath;
using reboot::os_linux::runner::KronWineRunner;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

TEST_CASE("the Wine runner layer only disables winemenubuilder", "[kron_wine_runner]") {
    const KronWineRunner runner{NativePath{"/rt"}, NativePath{"/rt/bin/wine"}};
    CHECK(runner.env() == EnvVars{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}});
}

TEST_CASE("the Wine port layout runs the wine loader", "[kron_wine_runner]") {
    const KronWineRunner runner{NativePath{"/rt"}, NativePath{"/rt/bin/wine"}};
    const reboot::ports::RuntimeLayout layout = runner.to_runtime_layout();
    CHECK(layout.root == NativePath{"/rt"});
    CHECK(layout.entry == NativePath{"/rt/bin/wine"});
    CHECK(layout.env == runner.env());
}
