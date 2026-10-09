#include "registrar_rules.hpp"

#include "reboot/foundation/paths.hpp"
#include "wide.hpp"

namespace rb::os_windows::platform {

std::optional<NativePath> command_program(std::string_view command) {
    const std::size_t start = command.find_first_not_of(' ');
    if (start == std::string_view::npos) return std::nullopt;
    command.remove_prefix(start);
    std::string_view program;
    if (command.front() == '"') {
        const std::size_t close = command.find('"', 1);
        if (close == std::string_view::npos) return std::nullopt;
        program = command.substr(1, close - 1);
    } else {
        program = command.substr(0, command.find(' '));
    }
    if (program.empty()) return std::nullopt;
    return NativePath(widen(program));
}

std::string entry_command(const NativePath& exe, std::string_view args) {
    std::string command = "\"" + narrow(exe.native()) + "\"";
    if (!args.empty()) {
        command += ' ';
        command += args;
    }
    return command;
}

bool startup_disabled(std::span<const u8> approved) noexcept { return !approved.empty() && (approved.front() & 1u) != 0; }

ports::IntegrationState ownership(const NativePath& program, bool program_exists, const NativePath& install_root) {
    if (!program_exists) return ports::IntegrationState::Stale;
    return is_inside(program, install_root) ? ports::IntegrationState::Ours : ports::IntegrationState::Foreign;
}

}  // namespace rb::os_windows::platform
