#pragma once

#include "reboot/base/diag.hpp"
#include "reboot/base/function.hpp"

namespace rb::game_channel {

// The single reply to one request, on the strand.
using ReplyHandler = UniqueFunction<void(Result<void>)>;

}  // namespace rb::game_channel
