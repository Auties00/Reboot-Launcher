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

namespace rb::ports {
class IFileSystem;
}

namespace rb::os_windows::platform {

// Covers no capability ids; ISecretStore over Credential Manager with a DPAPI file fallback.
class CredentialManagerStore final : public ports::ISecretStore {
public:
    // The root hash in the target name keeps two data roots apart; SSH and network logons have no
    // credential set, so those sessions use DPAPI files in `fallback_dir`.
    CredentialManagerStore(std::string root_hash16, NativePath fallback_dir, ports::IFileSystem& fs);

    [[nodiscard]] ports::SecretStoreKind kind() const override;
    // Writing removes any copy left in the other backend.
    Result<void> put(std::string_view key, std::span<const u8> value) override;
    // Falls back to the other backend, which a session of the other logon kind may have written.
    Result<std::optional<SecretBytes>> get(std::string_view key) override;
    Result<void> erase(std::string_view key) override;

private:
    std::string target_prefix_;
    NativePath fallback_dir_;
    ports::IFileSystem& fs_;
    bool credential_manager_available_ = false;
};

}  // namespace rb::os_windows::platform
