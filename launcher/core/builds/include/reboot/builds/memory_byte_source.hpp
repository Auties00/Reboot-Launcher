#pragma once

#include <span>

#include "reboot/builds/byte_source.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

// Borrows the bytes; they must outlive the source.
class MemoryByteSource final : public IByteSource {
public:
    explicit MemoryByteSource(std::span<const u8> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] u64 size() const noexcept override { return bytes_.size(); }
    Result<void> read_at(u64 offset, std::span<u8> out) override;

private:
    std::span<const u8> bytes_;
};

}  // namespace rb::builds
