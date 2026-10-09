#pragma once

#include <string>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

// The embedded backend's answer to Health, asked every time readiness matters.
struct BackendHealth {
    std::string version;
    contracts::backend::ContentVersion content;
    u16 http_port = 0;
};

}  // namespace rb::backend
