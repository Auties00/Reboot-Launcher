#include "reboot/gameserver/operator_command.hpp"

#include <algorithm>

namespace rb::gameserver {

bool is_declared(const OperatorCommand& command, const GameServerCapabilities& capabilities) {
    if (std::holds_alternative<ResetMatch>(command)) return capabilities.in_process_reset;
    const std::string_view name = command_name(command);
    return std::ranges::find(capabilities.operator_commands, name) != capabilities.operator_commands.end();
}

}  // namespace rb::gameserver
