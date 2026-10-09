#pragma once

#include <string>

#include "reboot/foundation/secret.hpp"
#include "reboot/ports/session_host.hpp"

namespace rb::play {

// Growing to capacity first also clears what a short-string move left behind.
inline void wipe(std::string& text) noexcept {
    text.resize(text.capacity());
    secure_wipe(text.data(), text.size());
    text.clear();
}

// A SessionLaunch copy carries the credential in argv and the control token in env.
inline void wipe(ports::SessionLaunch& launch) noexcept {
    for (std::string& arg : launch.args) wipe(arg);
    for (auto& [name, value] : launch.env.vars) wipe(value);
}

}  // namespace rb::play
