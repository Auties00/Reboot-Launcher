#pragma once

#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::components {

struct StoredFile {
    PayloadRole role{};
    NativePath path;
    Sha256Digest sha256{};
    u64 size = 0;
};

// A payload version in the store, with one file per role required_payload_roles() names for
// this platform; Windows never stores or verifies winhost.
struct PayloadSet {
    ComponentRef ref;
    u16 payload_abi = 0;
    std::vector<StoredFile> files;

    [[nodiscard]] const StoredFile* find(PayloadRole role) const noexcept {
        for (const StoredFile& file : files)
            if (file.role == role) return &file;
        return nullptr;
    }
};

}  // namespace rb::components
