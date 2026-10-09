#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fake_client_dll_script.hpp"
#include "reboot/testing/script_steps.hpp"

namespace rb::testing {

using WinhostStep =
    std::variant<ScriptPause, ScriptDisconnect, ScriptStopPonging, contracts::winhost::Spawned,
                 contracts::winhost::Injected, contracts::winhost::Output, contracts::winhost::Exited,
                 contracts::winhost::WhFatal, contracts::common::Log>;

// ERROR_ACCESS_DENIED, what an AV-blocked injection reports.
inline constexpr i64 kFakeInjectionError = 5;

// What FakeWinhost does. The defaults describe a conforming reboot-winhost.exe.
struct FakeWinhostScript {
    u16 payload_abi = contracts::game_client::kPayloadAbi;
    u32 protocol = contracts::winhost::kWinhostProtocol;
    std::string build = "fake";
    std::optional<std::array<u8, 32>> token_override;
    bool send_hello = true;
    std::chrono::milliseconds hello_delay{0};
    // After Welcome: Spawned for the game and each companion (pids from first_pid), then Injected
    // for every Early entry. Off, the test sends them itself.
    bool auto_spawn = true;
    u32 first_pid = 0x1000;
    // Inject entries, by file name, that report kFakeInjectionError instead of success.
    std::vector<std::string> failing_injections;
    // Run in order after Resume.
    std::vector<WinhostStep> after_resume;
    // After Resume, our DLL inside the game connects with this script, using the bootstrap in the
    // SpawnGame environment block.
    std::optional<FakeClientDllScript> game_client_dll;
    // Stop gets an ok CommandResult, then Exited{Game, code} and a disconnect.
    i64 exit_code_on_stop = 0;
};

}  // namespace rb::testing
