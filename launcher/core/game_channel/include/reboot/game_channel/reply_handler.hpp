#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"

namespace reboot::game_channel {

// The single reply to one request, on the strand.
using ReplyHandler = UniqueFunction<void(Result<void>)>;

}  // namespace reboot::game_channel
