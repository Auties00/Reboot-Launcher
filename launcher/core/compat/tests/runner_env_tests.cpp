#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "reboot/compat/runner_env.hpp"

using rb::compat::runner_layer;
using rb::ports::RunnerKind;
using rb::ports::RuntimeLayout;

using EnvVars = std::vector<std::pair<std::string, std::string>>;

namespace {

RuntimeLayout layout_with(EnvVars env) {
    RuntimeLayout layout;
    layout.env = std::move(env);
    return layout;
}

}  // namespace

TEST_CASE("every Wine runner disables winemenubuilder", "[compat][runner_env]") {
    for (const RunnerKind kind : {RunnerKind::Umu, RunnerKind::Wine, RunnerKind::MacRuntime})
        CHECK(runner_layer(kind, RuntimeLayout{}).vars == EnvVars{{"WINEDLLOVERRIDES", "winemenubuilder.exe=d"}});
}

TEST_CASE("the runtime's own variables come first and its overrides are merged", "[compat][runner_env]") {
    const auto layer = runner_layer(RunnerKind::Umu, layout_with({{"PROTONPATH", "/rt/ge-proton"},
                                                                  {"WINEDLLOVERRIDES", "mscoree,mshtml=;winemenubuilder=n"},
                                                                  {"GAMEID", "umu-default"}}));
    CHECK(layer.vars == EnvVars{{"PROTONPATH", "/rt/ge-proton"},
                                {"GAMEID", "umu-default"},
                                {"WINEDLLOVERRIDES", "mscoree,mshtml=;winemenubuilder.exe=d"}});
}

TEST_CASE("the macOS runtime never overrides DXMT's builtin DLLs", "[compat][runner_env]") {
    const RuntimeLayout layout = layout_with({{"WINEDLLOVERRIDES", "*d3d11,dxgi.dll=n,b;D3D10Core=n;winemetal=b;xinput1_3=n"}});
    CHECK(runner_layer(RunnerKind::MacRuntime, layout).vars ==
          EnvVars{{"WINEDLLOVERRIDES", "xinput1_3=n;winemenubuilder.exe=d"}});
    // Elsewhere those names are ordinary DLLs.
    CHECK(runner_layer(RunnerKind::Wine, layout).vars ==
          EnvVars{{"WINEDLLOVERRIDES", "*d3d11,dxgi.dll=n,b;D3D10Core=n;winemetal=b;xinput1_3=n;winemenubuilder.exe=d"}});
}
