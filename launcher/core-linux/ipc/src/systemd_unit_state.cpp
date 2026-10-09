#include "systemd_unit_state.hpp"

#include <cstddef>

namespace rb::os_linux::ipc {
namespace {

constexpr std::string_view kStreamSuffix = " (Stream)";

}  // namespace

SystemdUnitState SystemdUnitState::parse(std::string_view show_output) {
    SystemdUnitState state;
    while (!show_output.empty()) {
        const std::size_t end = show_output.find('\n');
        const std::string_view line = show_output.substr(0, end);
        show_output.remove_prefix(end == std::string_view::npos ? show_output.size() : end + 1);

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view name = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);
        if (name == "LoadState") {
            state.loaded = value == "loaded";
        } else if (name == "ActiveState") {
            state.running = value == "active" || value == "activating" || value == "reloading";
        } else if (name == "Listen" && value.ends_with(kStreamSuffix) && value.size() > kStreamSuffix.size()) {
            state.stream_paths.emplace_back(value.substr(0, value.size() - kStreamSuffix.size()));
        }
    }
    return state;
}

}  // namespace rb::os_linux::ipc
