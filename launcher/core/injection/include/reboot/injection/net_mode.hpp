#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::injection {

// Isolated: our DLL redirects to the front's per-session origin on an OS-assigned port.
// LegacyFixed: a custom auth DLL targets legacy_requirements(); it also sets
// SupportQuery::custom_auth_dll (Untested tier). One such session at a time is enforced
// atomically by backend::LegacyFixedArbiter::claim and front::LegacyFixedListeners::open.
enum class NetMode : u8 { Isolated, LegacyFixed };

}  // namespace rb::injection
