#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/script_steps.hpp"

namespace rb::testing {

using ClientDllStep =
    std::variant<ScriptPause, ScriptDisconnect, ScriptStopPonging, contracts::game_client::Loaded,
                 contracts::game_client::PatchResult, contracts::game_client::RedirectReady,
                 contracts::game_client::HookFailed, contracts::game_client::LoggedIn,
                 contracts::game_client::WindowCreated, contracts::game_client::ExitRequested,
                 contracts::game_client::ConsoleReady, contracts::game_client::Fatal, contracts::common::Log>;

// Loaded with an exact build match, RedirectReady, LoggedIn: the path that takes a play session
// to Running.
[[nodiscard]] std::vector<ClientDllStep> default_client_dll_steps();

// What FakeClientDll sends and how it answers. The defaults describe a conforming client DLL.
struct FakeClientDllScript {
    // The preamble's payload_abi and GcHello's protocol.
    u16 payload_abi = contracts::game_client::kPayloadAbi;
    u32 protocol = contracts::game_client::kPayloadAbi;
    std::string dll_build = "fake";
    contracts::game_client::GameBuild game;
    std::array<u8, 32> exe_sha256{};
    // Sent instead of the bootstrap token, for the wrong-token path.
    std::optional<std::array<u8, 32>> token_override;
    bool send_hello = true;
    std::chrono::milliseconds hello_delay{0};
    // Run in order once GcWelcome arrives.
    std::vector<ClientDllStep> after_welcome = default_client_dll_steps();
    // GcShutdown is answered with an ok CommandResult; then the DLL disconnects unless this is false.
    bool exit_on_shutdown = true;
};

}  // namespace rb::testing
