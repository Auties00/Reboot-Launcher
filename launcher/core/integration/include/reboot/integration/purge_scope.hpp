#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::integration {

// Settings, the library, accounts and host identities are never purged: Settings.reset and Library.remove own them.
enum class PurgeScope : u8 { BackendData, Logs, Cache, Components, All };

}  // namespace reboot::integration
