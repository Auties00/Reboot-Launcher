#include "engine_start_plan.hpp"

#include <array>

#include "engine_units.hpp"

namespace rb::os_linux::ipc {
namespace {

constexpr std::string_view kSystemctl = "systemctl";
constexpr std::string_view kSystemdRun = "systemd-run";
constexpr std::array<std::string_view, 2> kOnDemandArguments{"run", "--origin=on-demand"};

}  // namespace

std::vector<std::string> show_unit_argv(std::string_view unit) {
    return {std::string(kSystemctl), "--user", "show", "--property=LoadState,ActiveState,Listen", std::string(unit)};
}

SocketUnitAction socket_unit_action(const SystemdUnitState& state, const NativePath& socket_path,
                                    bool socket_present) {
    if (!state.loaded || state.stream_paths.size() != 1 || state.stream_paths.front() != socket_path)
        return SocketUnitAction::Skip;
    return state.running && !socket_present ? SocketUnitAction::Restart : SocketUnitAction::Start;
}

std::vector<std::string> socket_unit_argv(SocketUnitAction action) {
    return {std::string(kSystemctl), "--user", action == SocketUnitAction::Restart ? "restart" : "start",
            std::string(kEngineSocketUnit)};
}

std::string transient_unit_name(std::string_view root_hash16) {
    std::string unit(kTransientUnitPrefix);
    unit += root_hash16;
    return unit;
}

std::vector<std::string> engine_command(const NativePath& engine_exe, const std::optional<NativePath>& appimage) {
    std::vector<std::string> command;
    if (appimage) {
        command.push_back(appimage->string());
        command.emplace_back("engine");
    } else {
        command.push_back(engine_exe.string());
    }
    for (const std::string_view argument : kOnDemandArguments) command.emplace_back(argument);
    return command;
}

std::vector<std::string> systemd_run_argv(std::string_view unit, std::span<const std::string> pinned,
                                          std::span<const std::string> command) {
    std::vector<std::string> argv{std::string(kSystemdRun), "--user", "--unit=" + std::string(unit), "--collect",
                                  "--quiet"};
    for (const std::string& variable : pinned) argv.push_back("--setenv=" + variable);
    argv.emplace_back("--");
    argv.insert(argv.end(), command.begin(), command.end());
    return argv;
}

}  // namespace rb::os_linux::ipc
