#include "reboot/injection/dll_path_validator.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::injection {

namespace {

constexpr std::size_t kPeOffsetField = 0x3C;
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

[[nodiscard]] bool has_dll_extension(const NativePath& path) {
    constexpr std::u8string_view kDll = u8".dll";
    const std::u8string extension = path.extension().u8string();
    if (extension.size() != kDll.size()) return false;
    for (std::size_t i = 0; i < extension.size(); ++i) {
        const char8_t c = extension[i];
        const char8_t lower = c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c;
        if (lower != kDll[i]) return false;
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

}  // namespace

Result<PinnedDll> DllPathValidator::validate(const NativePath& path) const {
    if (auto name = check_name(path); !name) return std::unexpected(std::move(name.error()));
    auto bytes = fs_.read_all(path);
    if (!bytes) return std::unexpected(read_failure(path, std::move(bytes.error())));
    if (auto image = check_image(path, *bytes); !image) return std::unexpected(std::move(image.error()));
    return PinnedDll{path, sha256(*bytes)};
}

Result<std::optional<PinnedDll>> DllPathValidator::resolve(const std::optional<NativePath>& custom_auth_dll) const {
    if (!custom_auth_dll) return std::optional<PinnedDll>{};
    auto dll = validate(*custom_auth_dll);
    if (!dll) return std::unexpected(std::move(dll.error()));
    return std::optional<PinnedDll>{std::move(*dll)};
}

Result<void> DllPathValidator::check_name(const NativePath& path) {
    if (path.empty())
        return make_diag(ErrorDomain::Injection, msg::kDllPathEmpty).kind(ErrorKind::InvalidInput).fail();
    if (!has_dll_extension(path)) return std::unexpected(path_diag(msg::kDllNotDll, path, ErrorKind::InvalidInput));
    return {};
}

Result<void> DllPathValidator::check_image(const NativePath& path, std::span<const u8> image) {
    const auto not_pe64 = [&] { return std::unexpected(path_diag(msg::kDllNotPe64, path, ErrorKind::InvalidInput)); };
    if (image.size() < kPeOffsetField + 4 || image[0] != 'M' || image[1] != 'Z') return not_pe64();
    const std::size_t pe = read_u32(image, kPeOffsetField);
    // Signature (4), COFF header (20), then the optional header's magic (2).
    if (pe > image.size() || image.size() - pe < 26) return not_pe64();
    if (image[pe] != 'P' || image[pe + 1] != 'E' || image[pe + 2] != 0 || image[pe + 3] != 0) return not_pe64();
    if (read_u16(image, pe + 4) != kMachineAmd64 || read_u16(image, pe + 24) != kPe32PlusMagic) return not_pe64();
    if ((read_u16(image, pe + 22) & kImageFileDll) == 0)
        return std::unexpected(path_diag(msg::kDllNotDll, path, ErrorKind::InvalidInput));
    return {};
}

}  // namespace reboot::injection
