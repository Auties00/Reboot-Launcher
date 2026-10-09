#pragma once

#include <string>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Ready was received and this generation's replay was acknowledged.
struct BackendReady {
    u32 generation = 0;
    u16 http_port = 0;
    // BackendHello.build.
    std::string build;
    contracts::backend::ContentVersion content;
};

}  // namespace rb::backend
