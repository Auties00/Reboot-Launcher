#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/testing/fake_client_dll_script.hpp"

namespace rb::testing {

// What reboot-fake-game does where FortniteClient-Win64-Shipping.exe stands: spawned suspended and
// injected into by winhost or win32session.
struct FakeGameScript {
    // Plays our client DLL itself from the bootstrap in its own environment; off when a real one is
    // injected.
    bool act_as_client_dll = true;
    FakeClientDllScript client_dll;
    // Module file names that must be loaded when main runs; each missing one is reported as
    // HookFailed{step = name, required = true}.
    std::vector<std::string> expect_modules;
    // Loaded by the fake itself after start, standing in for a LoggedIn-phase injection.
    std::vector<std::string> load_dlls;
    // Printed to stdout one per line, e.g. the UE log markers LegacyOutputAdapter reads.
    std::vector<std::string> output_lines;
    std::chrono::milliseconds output_interval{0};
    // Without it the game runs until GcShutdown or until its Job is killed.
    std::optional<std::chrono::milliseconds> exit_after;
    int exit_code = 0;
};

// The JSON form follows parse_fake_backend_script's rules.
[[nodiscard]] Result<FakeGameScript> parse_fake_game_script(std::string_view json);
[[nodiscard]] Result<FakeGameScript> load_fake_game_script(const NativePath& file);
[[nodiscard]] std::string to_json(const FakeGameScript& script);

}  // namespace rb::testing
