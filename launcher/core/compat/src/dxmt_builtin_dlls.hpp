#pragma once

#include <array>
#include <string_view>

namespace rb::compat {

// DXMT ships these as builtin DLLs in the macOS runtime; an n,b override would bypass it.
inline constexpr std::array<std::string_view, 4> kDxmtBuiltinDlls{"dxgi", "d3d11", "d3d10core", "winemetal"};

}  // namespace rb::compat
