#pragma once

#include <optional>

#include "reboot/foundation/native_path.hpp"

namespace reboot::testing {

// Where the build put the fake executables, for contract, ChildSupervisor and OS conformance runs;
// nullopt when this build has none.
[[nodiscard]] std::optional<NativePath> fake_backend_exe();
[[nodiscard]] std::optional<NativePath> fake_game_server_exe();
// Built on Windows only; elsewhere the REBOOT_FAKE_GAME_EXE CMake cache variable names a Windows build.
[[nodiscard]] std::optional<NativePath> fake_game_exe();

}  // namespace reboot::testing
