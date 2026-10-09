#include "reboot/testing/fake_executables.hpp"

#include <optional>

#include "reboot/foundation/native_path.hpp"

namespace rb::testing {

std::optional<NativePath> fake_backend_exe() {
#ifdef REBOOT_FAKE_BACKEND_EXE
    return NativePath(REBOOT_FAKE_BACKEND_EXE);
#else
    return std::nullopt;
#endif
}

std::optional<NativePath> fake_game_server_exe() {
#ifdef REBOOT_FAKE_GAME_SERVER_EXE
    return NativePath(REBOOT_FAKE_GAME_SERVER_EXE);
#else
    return std::nullopt;
#endif
}

std::optional<NativePath> fake_game_exe() {
#ifdef REBOOT_FAKE_GAME_EXE
    return NativePath(REBOOT_FAKE_GAME_EXE);
#else
    return std::nullopt;
#endif
}

}  // namespace rb::testing
