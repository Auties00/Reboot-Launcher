#include "env_vars.hpp"

#include <algorithm>
#include <utility>

namespace reboot::os_linux::runner {

void set_var(ports::EnvBlock& env, std::string_view name, std::string value) {
    std::erase_if(env.vars, [&](const auto& var) { return var.first == name; });
    env.vars.emplace_back(std::string(name), std::move(value));
}

std::string_view value_of(const ports::EnvBlock& env, std::string_view name) {
    const auto found = std::ranges::find_if(env.vars, [&](const auto& var) { return var.first == name; });
    return found == env.vars.end() ? std::string_view{} : std::string_view{found->second};
}

}  // namespace reboot::os_linux::runner
