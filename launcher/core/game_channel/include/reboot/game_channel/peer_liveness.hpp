#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::game_channel {

// Reported only; the session ignores Unresponsive while the game is Loading or Traveling.
enum class PeerLiveness : u8 { Responsive, Unresponsive };

}  // namespace reboot::game_channel
