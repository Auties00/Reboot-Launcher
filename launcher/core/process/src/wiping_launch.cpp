#include "reboot/process/wiping_launch.hpp"

#include "wipe.hpp"

namespace reboot::process {

namespace {

void wipe_env(ports::EnvBlock& env) noexcept {
    for (auto& [name, value] : env.vars) wipe_string(value);
    env.vars.clear();
}

}  // namespace

WipingLaunch& WipingLaunch::operator=(WipingLaunch&& other) noexcept {
    if (this != &other) {
        wipe_env(launch_.env);
        launch_ = std::move(other.launch_);
        other.launch_.env.vars.clear();
    }
    return *this;
}

WipingLaunch::~WipingLaunch() { wipe_env(launch_.env); }

}  // namespace reboot::process
