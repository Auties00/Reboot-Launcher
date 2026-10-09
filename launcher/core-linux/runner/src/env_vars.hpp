#pragma once

#include <string>
#include <string_view>

#include "reboot/ports/process.hpp"

namespace rb::os_linux::runner {

// Replaces every entry named `name`.
void set_var(ports::EnvBlock& env, std::string_view name, std::string value);

// Empty when `name` is unset.
[[nodiscard]] std::string_view value_of(const ports::EnvBlock& env, std::string_view name);

}  // namespace rb::os_linux::runner
