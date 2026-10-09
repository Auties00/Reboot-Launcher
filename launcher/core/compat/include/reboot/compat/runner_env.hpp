#pragma once

#include <string_view>

#include "reboot/compat/runner_profile.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::compat {

// Keeps Wine from writing .desktop launchers and MIME entries into the user's menus.
inline constexpr std::string_view kWineDllOverrides = "winemenubuilder.exe=d";

// Covers no capability ids (decisions linux-compat-layer, macos-compat-layer).
// EnvBuilder's runner layer for every Wine launch of `kind`, prefix commands included:
// RuntimeLayout::env with kWineDllOverrides merged into WINEDLLOVERRIDES. Under MacRuntime an
// override naming one of DXMT's DLLs is dropped, since DXMT serves them as builtin DLLs.
[[nodiscard]] ports::EnvBlock runner_layer(RunnerKind kind, const ports::RuntimeLayout& layout);

}  // namespace reboot::compat
