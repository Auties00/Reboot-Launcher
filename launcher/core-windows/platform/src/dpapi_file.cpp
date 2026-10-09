#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "dpapi_file.hpp"

#include "reboot/foundation/sha256.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

[[nodiscard]] DATA_BLOB blob_of(std::span<const u8> bytes) noexcept {
    return DATA_BLOB{static_cast<DWORD>(bytes.size()), const_cast<BYTE*>(bytes.data())};
}

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) noexcept {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

}  // namespace

NativePath dpapi_file_name(std::string_view target) { return NativePath(to_hex(sha256(bytes_of(target))) + ".secret"); }

Result<std::vector<u8>> dpapi_seal(std::span<const u8> plain, std::string_view target) {
    DATA_BLOB in = blob_of(plain);
    DATA_BLOB entropy = blob_of(bytes_of(target));
    DATA_BLOB out{};
    if (CryptProtectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out) == 0)
        return std::unexpected(call_failed("CryptProtectData", GetLastError()));
    std::vector<u8> sealed(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return sealed;
}

Result<SecretBytes> dpapi_open(std::span<const u8> sealed, std::string_view target) {
    DATA_BLOB in = blob_of(sealed);
    DATA_BLOB entropy = blob_of(bytes_of(target));
    DATA_BLOB out{};
    if (CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out) == 0)
        return std::unexpected(call_failed("CryptUnprotectData", GetLastError()));
    SecretBytes plain(std::vector<u8>(out.pbData, out.pbData + out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

}  // namespace rb::os_windows::platform
