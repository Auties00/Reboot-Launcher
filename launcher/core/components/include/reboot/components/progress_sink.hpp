#pragma once

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"

namespace rb::components {

using ProgressSink = UniqueFunction<void(const Progress&)>;

}  // namespace rb::components
