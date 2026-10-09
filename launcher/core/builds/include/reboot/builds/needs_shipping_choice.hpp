#pragma once

#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace rb::builds {

// More than one shipping exe; the caller resolves or imports again with one of `candidates`.
struct NeedsShippingChoice {
    NativePath folder;
    // Relative to `folder`.
    std::vector<NativePath> candidates;

    bool operator==(const NeedsShippingChoice&) const = default;
};

}  // namespace rb::builds
