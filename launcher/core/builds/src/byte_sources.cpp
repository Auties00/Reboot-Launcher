#include <algorithm>
#include <filesystem>
#include <ios>
#include <system_error>
#include <utility>

#include "builds_error.hpp"
#include "reboot/builds/file_byte_source.hpp"
#include "reboot/builds/memory_byte_source.hpp"

namespace rb::builds {

namespace {

[[nodiscard]] bool in_range(u64 size, u64 offset, std::size_t length) noexcept {
    return offset <= size && length <= size - offset;
}

[[nodiscard]] Diagnostic io_error(const NativePath& path, std::optional<SystemError> os_error = std::nullopt) {
    return to_diagnostic(BuildsError{.code = BuildsErrorCode::Io, .path = path, .os_error = os_error});
}

}  // namespace

Result<void> MemoryByteSource::read_at(u64 offset, std::span<u8> out) {
    if (!in_range(bytes_.size(), offset, out.size())) return std::unexpected(io_error({}));
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
    return {};
}

Result<FileByteSource> FileByteSource::open(const NativePath& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return std::unexpected(io_error(path, SystemError{.code = error.value()}));
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::unexpected(io_error(path));
    return FileByteSource(std::move(stream), path, static_cast<u64>(size));
}

Result<void> FileByteSource::read_at(u64 offset, std::span<u8> out) {
    if (!in_range(size_, offset, out.size())) return std::unexpected(io_error(path_));
    if (out.empty()) return {};
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset));
    stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!stream_ || static_cast<std::size_t>(stream_.gcount()) != out.size()) return std::unexpected(io_error(path_));
    return {};
}

}  // namespace rb::builds
