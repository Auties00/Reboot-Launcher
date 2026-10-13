#pragma once

#include <string>
#include <utility>
#include <vector>

#include "reboot/base/diag.hpp"

namespace rb::ux {

// Text a UI renders from its catalog; core never formats it.
struct MessageText {
    MessageId id;
    std::vector<std::pair<std::string, Arg>> args;
};

}  // namespace rb::ux
