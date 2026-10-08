#pragma once

#include <string>
#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::components {

// Every artifact the manifest names. `urls` are tried in order; `size` caps the download
// before the digest can be checked.
struct RemoteFile {
    std::vector<std::string> urls;
    Sha256Digest sha256{};
    u64 size = 0;

    bool operator==(const RemoteFile&) const = default;
};

}  // namespace reboot::components
