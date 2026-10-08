#pragma once

#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"

namespace reboot::ux {

// Text a UI renders from its catalog; core never formats it.
struct MessageText {
    MessageId id;
    std::vector<std::pair<std::string, Arg>> args;
};

}  // namespace reboot::ux
