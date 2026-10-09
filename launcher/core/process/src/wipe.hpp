#pragma once

#include <string>

#include "reboot/foundation/secret.hpp"

namespace rb::process {

// Growing to capacity first also clears bytes a short-string move left behind.
inline void wipe_string(std::string& text) noexcept {
    text.resize(text.capacity());
    secure_wipe(text.data(), text.size());
    text.clear();
}

}  // namespace rb::process
