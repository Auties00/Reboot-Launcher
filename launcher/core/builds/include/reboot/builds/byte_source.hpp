#pragma once

#include <span>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

// Random access over bytes, so the PE reader and the archive probe run unchanged over a file,
// a test buffer or a fuzz input.
class IByteSource {
public:
    virtual ~IByteSource() = default;

    [[nodiscard]] virtual u64 size() const noexcept = 0;
    // Fills all of `out` or fails; a range past size() fails with builds.io.
    virtual Result<void> read_at(u64 offset, std::span<u8> out) = 0;
};

}  // namespace rb::builds
