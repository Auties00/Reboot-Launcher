#pragma once

#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/game_server_config.hpp"
#include "reboot/gameserver/game_server_description.hpp"

namespace reboot::gameserver {

struct StartMatch {
    std::chrono::seconds countdown{};
};
struct EndMatch {};
// Keeps the bound port block.
struct ResetMatch {};
struct Kick {
    u32 player_id = 0;
    std::string reason;
};
struct SetBans {
    std::vector<Ban> bans;
};
struct SetOperators {
    std::vector<std::string> ip_cidrs;
};
struct RunCommand {
    std::string text;
};
struct Drain {};

// Every operator request, in contract frame order. Shutdown is not one: only stop() sends it.
using OperatorCommand = std::variant<StartMatch, EndMatch, ResetMatch, Kick, SetBans, SetOperators, RunCommand, Drain>;

// The names a description lists in capabilities.operator_commands, by variant index. "reset" is
// never listed: in_process_reset declares it.
inline constexpr std::array<std::string_view, std::variant_size_v<OperatorCommand>> kOperatorCommandNames{
    "start_match", "end_match", "reset", "kick", "set_bans", "set_operators", "run_command", "drain"};

[[nodiscard]] constexpr std::string_view command_name(const OperatorCommand& command) noexcept {
    return kOperatorCommandNames[command.index()];
}

// ResetMatch when in_process_reset; any other command only when its name is listed.
[[nodiscard]] bool is_declared(const OperatorCommand& command, const GameServerCapabilities& capabilities);

}  // namespace reboot::gameserver
