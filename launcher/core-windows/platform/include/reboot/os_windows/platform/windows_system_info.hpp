#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; ISystemInfo for the engine process, read once at construction.
class WindowsSystemInfo final : public ports::ISystemInfo {
public:
    WindowsSystemInfo();

    // RtlGetVersion, since GetVersionEx lies without a manifest; IsWow64Process2 shows x64 on ARM64.
    [[nodiscard]] ports::OsInfo os() const override;
    [[nodiscard]] bool elevated() const override;
    [[nodiscard]] std::string os_session() const override;
    [[nodiscard]] bool under_steam_reaper() const override { return false; }
    // curl uses Schannel, which trusts the Windows root store.
    [[nodiscard]] std::optional<NativePath> ca_bundle() const override { return std::nullopt; }

private:
    ports::OsInfo os_;
    bool elevated_ = false;
    std::string os_session_;
};

}  // namespace reboot::os_windows::platform
