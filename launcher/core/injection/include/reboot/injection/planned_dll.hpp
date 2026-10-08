#pragma once

#include "reboot/injection/dll_slot.hpp"
#include "reboot/ports/session_host.hpp"

namespace reboot::injection {

struct PlannedDll {
    DllSlot slot{};
    ports::InjectEntry entry;
};

}  // namespace reboot::injection
