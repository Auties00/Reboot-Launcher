#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::builds {

// The ChooseVersion payload; the answer is a UserVersion, picked from the catalog's known versions.
struct ChooseVersionPrompt {
    std::string name;
    NativePath root;
    std::optional<std::string> raw;
    std::vector<Diagnostic> reasons;
};

}  // namespace rb::builds
