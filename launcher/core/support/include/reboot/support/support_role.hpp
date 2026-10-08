#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::support {

// Play runs the Windows game with our client DLL; Host runs our native game server.
enum class SupportRole : u8 { Play, Host };

}  // namespace reboot::support
