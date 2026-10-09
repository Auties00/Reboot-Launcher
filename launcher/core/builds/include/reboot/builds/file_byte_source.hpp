#pragma once

#include <fstream>
#include <span>
#include <utility>

#include "reboot/builds/byte_source.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

// Plain std::ifstream seek and read; blocking.
class FileByteSource final : public IByteSource {
public:
    [[nodiscard]] static Result<FileByteSource> open(const NativePath& path);

    [[nodiscard]] u64 size() const noexcept override { return size_; }
    Result<void> read_at(u64 offset, std::span<u8> out) override;

private:
    FileByteSource(std::ifstream stream, NativePath path, u64 size)
        : stream_(std::move(stream)), path_(std::move(path)), size_(size) {}

    std::ifstream stream_;
    NativePath path_;
    u64 size_ = 0;
};

}  // namespace rb::builds
