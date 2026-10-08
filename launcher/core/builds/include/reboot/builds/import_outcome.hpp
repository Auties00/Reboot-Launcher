#pragma once

#include <variant>

#include "reboot/builds/installed_build.hpp"
#include "reboot/builds/needs_shipping_choice.hpp"
#include "reboot/builds/version_detection.hpp"

namespace reboot::builds {

struct Imported {
    InstalledBuild build;
};

// NeedsUserVersion only when the request said not to ask, as the non-interactive CLI does.
using ImportOutcome = std::variant<Imported, NeedsUserVersion, NeedsShippingChoice>;

}  // namespace reboot::builds
