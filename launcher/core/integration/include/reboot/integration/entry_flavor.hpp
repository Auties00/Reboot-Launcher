#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::integration {

// macOS declares the scheme and agents in the bundle, so Apple entries carry no command.
enum class EntryFlavor : u8 { Windows, FreeDesktop, Apple };

}  // namespace rb::integration
