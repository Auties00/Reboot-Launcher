#pragma once

#include <chrono>
#include <memory>
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

namespace rb::os_linux::platform {

// Covers no capability ids; ISecretStore over the Secret Service through libsecret, falling
// back to 0600 files in a 0700 directory. Those files are plaintext, unlike Windows' DPAPI
// fallback, so secrets must not Remember into kind File without a file_insecure opt-in.
class LibsecretSecretStore final : public ports::ISecretStore {
public:
    // Reaching the session bus and a Secret Service provider.
    inline static constexpr std::chrono::seconds kConnectDeadline{5};
    // A call into a locked collection waits on a human unlock prompt.
    inline static constexpr std::chrono::seconds kPromptDeadline{120};

    // Only dlopens libsecret-1.so.0, so the engine never links GLib. Items use the schema
    // "dev.projectreboot.Launcher" with attributes root=<root_hash16> and key=<key>.
    // `fallback_dir` is <data root>/state/secrets.
    LibsecretSecretStore(std::string root_hash16, NativePath fallback_dir, ports::IFileSystem& fs);
    ~LibsecretSecretStore() override;
    LibsecretSecretStore(const LibsecretSecretStore&) = delete;
    LibsecretSecretStore& operator=(const LibsecretSecretStore&) = delete;

    // Os while the Secret Service answers, File while it does not. Never Unavailable.
    [[nodiscard]] ports::SecretStoreKind kind() const override;
    // In File mode each put probes the Secret Service again, since a lingering engine may start
    // before the session bus. A missing library, bus or provider writes the file; a deadline
    // passed once connected fails with platform.secret_service_timeout and never falls back.
    // Values are base64 through secret_password_storev_sync, which EL8's libsecret 0.18 has.
    // Writing removes any copy in the other backend.
    Result<void> put(std::string_view key, std::span<const u8> value) override;
    // The active backend first, then the other one.
    Result<std::optional<SecretBytes>> get(std::string_view key) override;
    // Erases from both backends; a missing key is success.
    Result<void> erase(std::string_view key) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::os_linux::platform
