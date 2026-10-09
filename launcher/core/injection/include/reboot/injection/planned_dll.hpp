#pragma once

#include "reboot/injection/dll_slot.hpp"
#include "reboot/ports/session_host.hpp"

namespace rb::injection {

struct PlannedDll {
    DllSlot slot{};
    ports::InjectEntry entry;
};

}  // namespace rb::injection
