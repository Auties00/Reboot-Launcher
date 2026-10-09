#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::platform {

inline constexpr std::string_view kEngineSocketUnit = "reboot-engine.socket";
inline constexpr std::string_view kEngineServiceUnit = "reboot-engine.service";

// ListenStream of the engine socket for `hash16`, under the user manager's %t.
[[nodiscard]] std::string engine_listen_stream(std::string_view hash16);

[[nodiscard]] std::string render_engine_socket(std::string_view hash16);

// `exec_start` is the program and its arguments; the XDG homes are pinned in the environment.
[[nodiscard]] std::string render_engine_service(const std::vector<std::string>& exec_start, const NativePath& data_home,
                                                const NativePath& cache_home, const NativePath& state_home);

// The raw value of the first `key=` line, in any group.
[[nodiscard]] std::optional<std::string> unit_value(std::string_view text, std::string_view key);

// One ExecStart argument: '%' and '$' doubled, quoted with C escapes where needed.
[[nodiscard]] std::string unit_quote(std::string_view arg);
// The arguments of an ExecStart value; nullopt when a quote is unbalanced.
[[nodiscard]] std::optional<std::vector<std::string>> split_unit_command(std::string_view value);

}  // namespace reboot::os_linux::platform
