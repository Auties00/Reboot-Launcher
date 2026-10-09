#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::os_linux::platform {

// The value of `name` in this process's environment; nullopt when unset.
[[nodiscard]] std::optional<std::string_view> env_value(const char* name) noexcept;

// This process's environment as NAME=value pairs, in environ order.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> current_environment();

// "NAME=value" strings for an envp; `vars` later in the list win over earlier ones of the same name.
[[nodiscard]] std::vector<std::string> envp_strings(const std::vector<std::pair<std::string, std::string>>& vars);

}  // namespace rb::os_linux::platform
