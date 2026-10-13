#include "reboot/injection/dll_path_validator.hpp"

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::injection {

namespace {

constexpr std::size_t kPeOffsetField = 0x3C;
constexpr std::size_t kDosHeaderSize = kPeOffsetField + 4;
// Signature (4), COFF header (20), then the optional header's magic (2).
constexpr std::size_t kPeHeaderSize = 26;
constexpr u16 kMachineAmd64 = 0x8664;
constexpr u16 kPe32PlusMagic = 0x20B;
constexpr u16 kImageFileDll = 0x2000;

[[nodiscard]] u16 read_u16(std::span<const u8> image, std::size_t offset) noexcept {
    return static_cast<u16>(image[offset] | (image[offset + 1] << 8));
}

[[nodiscard]] u32 read_u32(std::span<const u8> image, std::size_t offset) noexcept {
    return static_cast<u32>(image[offset]) | (static_cast<u32>(image[offset + 1]) << 8) |
           (static_cast<u32>(image[offset + 2]) << 16) | (static_cast<u32>(image[offset + 3]) << 24);
}

// Compares native units: converting to UTF-8 throws on Windows for an unpaired surrogate.
[[nodiscard]] bool has_dll_extension(const NativePath& path) {
    using Unit = NativePath::value_type;
    constexpr std::string_view kDll = ".dll";
    const NativePath::string_type extension = path.extension().native();
    if (extension.size() != kDll.size()) return false;
    for (std::size_t i = 0; i < extension.size(); ++i) {
        const Unit c = extension[i];
        const Unit lower = c >= Unit{'A'} && c <= Unit{'Z'} ? static_cast<Unit>(c - Unit{'A'} + Unit{'a'}) : c;
        if (lower != static_cast<Unit>(kDll[i])) return false;
    }
    return true;
}

[[nodiscard]] Diagnostic path_diag(MessageId message, const NativePath& path, ErrorKind kind) {
    return make_diag(ErrorDomain::Injection, message).arg("path", path).kind(kind).build();
}

// NotFound is a missing file; anything else keeps its OS code as unreadable.
[[nodiscard]] Diagnostic read_failure(const NativePath& path, Diagnostic read) {
    const bool missing = read.kind == ErrorKind::NotFound;
    const std::optional<SystemError> os = read.os_error;
    Diagnostic diag = make_diag(ErrorDomain::Injection, missing ? msg::kDllMissing : msg::kDllUnreadable)
                          .arg("path", path)
                          .kind(missing ? ErrorKind::NotFound : ErrorKind::Generic)
                          .cause(std::move(read))
                          .build();
    diag.os_error = os;
    return diag;
}

[[nodiscard]] Diagnostic not_pe64(const NativePath& path) {
    return path_diag(msg::kDllNotPe64, path, ErrorKind::InvalidInput);
}

// The PE header's offset from the DOS header's e_lfanew.
[[nodiscard]] Result<u32> pe_offset(const NativePath& path, std::span<const u8> dos) {
    if (dos.size() < kDosHeaderSize || dos[0] != 'M' || dos[1] != 'Z') return std::unexpected(not_pe64(path));
    return read_u32(dos, kPeOffsetField);
}

// `header` starts at the PE signature.
[[nodiscard]] Result<void> check_pe_header(const NativePath& path, std::span<const u8> header) {
    if (header.size() < kPeHeaderSize) return std::unexpected(not_pe64(path));
    if (header[0] != 'P' || header[1] != 'E' || header[2] != 0 || header[3] != 0) return std::unexpected(not_pe64(path));
    if (read_u16(header, 4) != kMachineAmd64 || read_u16(header, 24) != kPe32PlusMagic)
        return std::unexpected(not_pe64(path));
    if ((read_u16(header, 22) & kImageFileDll) == 0)
        return std::unexpected(path_diag(msg::kDllNotDll, path, ErrorKind::InvalidInput));
    return {};
}

}  // namespace

Result<void> DllPathValidator::validate(const NativePath& path) const {
    if (auto name = check_name(path); !name) return std::unexpected(std::move(name.error()));
    auto dos = fs_.read_shared(path, 0, kDosHeaderSize);
    if (!dos) return std::unexpected(read_failure(path, std::move(dos.error())));
    const Result<u32> pe = pe_offset(path, dos->bytes);
    if (!pe) return std::unexpected(pe.error());
    auto header = fs_.read_shared(path, *pe, kPeHeaderSize);
    if (!header) return std::unexpected(read_failure(path, std::move(header.error())));
    return check_pe_header(path, header->bytes);
}

Result<void> DllPathValidator::check_name(const NativePath& path) {
    if (path.empty())
        return make_diag(ErrorDomain::Injection, msg::kDllPathEmpty).kind(ErrorKind::InvalidInput).fail();
    if (!has_dll_extension(path)) return std::unexpected(path_diag(msg::kDllNotDll, path, ErrorKind::InvalidInput));
    return {};
}

Result<void> DllPathValidator::check_image(const NativePath& path, std::span<const u8> image) {
    const Result<u32> pe = pe_offset(path, image);
    if (!pe) return std::unexpected(pe.error());
    if (*pe > image.size()) return std::unexpected(not_pe64(path));
    return check_pe_header(path, image.subspan(*pe));
}

}  // namespace rb::injection
