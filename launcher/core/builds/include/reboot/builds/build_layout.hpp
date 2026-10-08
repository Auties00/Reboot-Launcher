#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace reboot::builds {

inline constexpr std::string_view kShippingExe = "FortniteClient-Win64-Shipping.exe";
inline constexpr std::string_view kLauncherExe = "FortniteLauncher.exe";
inline constexpr std::string_view kEacExe = "FortniteClient-Win64-Shipping_EAC.exe";
inline constexpr std::string_view kCrashReportClientExe = "CrashReportClient.exe";
inline constexpr std::string_view kAftermathDll = "GFSDK_Aftermath_Lib.dll";

// Every file name the one layout walk collects.
inline constexpr std::array<std::string_view, 5> kLayoutTargets{kShippingExe, kLauncherExe, kEacExe,
                                                                kCrashReportClientExe, kAftermathDll};

// Where a build's files are. `root` is absolute and host-native (never Binaries\Win64); the
// other paths are relative to it, with the casing found on disk.
struct BuildLayout {
    NativePath root;
    NativePath shipping_exe;
    std::optional<NativePath> launcher_exe;
    std::optional<NativePath> eac_exe;
    // Ranked: the one under Engine/Binaries/Win64 first, then by path.
    std::vector<NativePath> crash_report_clients;
    // Never deleted; play::parked_for moves them aside for each play session.
    std::vector<NativePath> aftermath_dlls;

    [[nodiscard]] NativePath binaries_dir() const { return root / shipping_exe.parent_path(); }

    bool operator==(const BuildLayout&) const = default;
};

}  // namespace reboot::builds
