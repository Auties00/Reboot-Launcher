#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::testing {

// This process's id, for Hello frames and the port binder.
[[nodiscard]] u32 current_process_id() noexcept;

}  // namespace rb::testing
