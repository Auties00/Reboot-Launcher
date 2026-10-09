#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_windows::platform {

// The fallback file for a credential target: the sha256 of its name, so any key is a valid file name.
[[nodiscard]] NativePath dpapi_file_name(std::string_view target);

// CryptProtectData for the current user, bound to `target` as entropy, so a file renamed to another
// key's name does not decrypt.
[[nodiscard]] Result<std::vector<u8>> dpapi_seal(std::span<const u8> plain, std::string_view target);
[[nodiscard]] Result<SecretBytes> dpapi_open(std::span<const u8> sealed, std::string_view target);

}  // namespace reboot::os_windows::platform
