#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::injection {

// Capabilities: dll-injection.dll-set-resolution.
// ClientRuntime is rb_client.dll from the pinned payload; CustomAuth is the only user DLL slot.
// The spec's ExtraDll is not modelled: owner decision 5 keeps only a custom auth DLL.
enum class DllSlot : u8 { ClientRuntime, CustomAuth };

}  // namespace rb::injection
