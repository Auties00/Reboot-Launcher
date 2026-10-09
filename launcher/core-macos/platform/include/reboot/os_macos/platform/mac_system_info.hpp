#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::ports {
class IFileSystem;
}

namespace rb::os_macos::platform {

// Covers no capability ids; ISystemInfo for the engine process, read once at engine start.
class MacSystemInfo final : public ports::ISystemInfo {
public:
    // Exports the SSL trust anchors as `trust_dir`/apple-roots.pem for OpenSSL (curl, MsQuic).
    // Roots that admin or user trust settings mark Deny are left out of the export.
    MacSystemInfo(const NativePath& trust_dir, ports::IFileSystem& fs);

    // The arch comes from hw.optional.arm64, so an engine under Rosetta still reports arm64.
    [[nodiscard]] ports::OsInfo os() const override;
    [[nodiscard]] bool elevated() const override;
    // The audit session id, which core-macos/ipc's caller probe reports for a client.
    [[nodiscard]] std::string os_session() const override;
    [[nodiscard]] bool under_steam_reaper() const override { return false; }
    // Empty when the export failed.
    [[nodiscard]] std::optional<NativePath> ca_bundle() const override { return ca_bundle_; }

    // A logged-in desktop session, not SSH or a Background launchd session.
    [[nodiscard]] bool aqua() const noexcept { return aqua_; }

private:
    ports::OsInfo os_;
    bool elevated_ = false;
    std::string os_session_;
    bool aqua_ = false;
    std::optional<NativePath> ca_bundle_;
};

}  // namespace rb::os_macos::platform
