#pragma once

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"

namespace reboot::components {

using ProgressSink = UniqueFunction<void(const Progress&)>;

}  // namespace reboot::components
