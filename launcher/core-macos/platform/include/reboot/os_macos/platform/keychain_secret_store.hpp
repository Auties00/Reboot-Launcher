#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/secret_store.hpp"

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::os_macos::platform {

// Covers no capability ids; ISecretStore over the file-based login keychain, 0600 files as fallback.
class KeychainSecretStore final : public ports::ISecretStore {
public:
    // `fallback_dir` is <data root>/state/secrets; `aqua_session` is MacSystemInfo::aqua().
    // Turns keychain interaction off process-wide, so a locked keychain fails instead of prompting.
    KeychainSecretStore(std::string root_hash16, NativePath fallback_dir, ports::IFileSystem& fs, bool aqua_session);

    // File when there is no login keychain, or it is locked outside Aqua where nobody can unlock it.
    [[nodiscard]] ports::SecretStoreKind kind() const override;
    // The item's ACL names the engine's designated requirement, so a same-team update reads it silently.
    // In Aqua a locked keychain fails with platform.keychain_locked rather than writing a file.
    Result<void> put(std::string_view key, std::span<const u8> value) override;
    // A key missing from the files while the keychain is locked fails with platform.keychain_locked.
    Result<std::optional<SecretBytes>> get(std::string_view key) override;
    // Erases from both backends; a missing key is success.
    Result<void> erase(std::string_view key) override;

private:
    std::string service_;
    NativePath fallback_dir_;
    ports::IFileSystem& fs_;
    bool aqua_session_ = false;
};

}  // namespace reboot::os_macos::platform
