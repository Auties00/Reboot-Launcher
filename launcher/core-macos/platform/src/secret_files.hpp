#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ports {
class IFileSystem;
}

namespace rb::os_macos::platform {

// The 0600 file fallback of KeychainSecretStore: one file per key in a 0700 directory.
class SecretFiles {
public:
    SecretFiles(NativePath dir, ports::IFileSystem& fs) noexcept;

    Result<void> put(std::string_view key, std::span<const u8> value);
    // Nullopt when the key has no file.
    Result<std::optional<SecretBytes>> get(std::string_view key);
    // A missing key is success.
    Result<void> erase(std::string_view key);

    // <dir>/<sha256(key) in hex>.secret, so any key makes a safe, bounded file name.
    [[nodiscard]] NativePath path_of(std::string_view key) const;

private:
    NativePath dir_;
    ports::IFileSystem& fs_;
};

}  // namespace rb::os_macos::platform
