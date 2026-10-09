#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::components {

enum class ManifestOs : u8 { Windows, MacOs, Linux };
enum class ManifestArch : u8 { X64, Arm64 };

struct ManifestPlatform {
    ManifestOs os{};
    ManifestArch arch{};

    constexpr bool operator==(const ManifestPlatform&) const = default;
};

// The platform this binary targets, which selects its app and runtime entries.
[[nodiscard]] constexpr ManifestPlatform build_platform() noexcept {
#if defined(_WIN32)
    constexpr ManifestOs os = ManifestOs::Windows;
#elif defined(__APPLE__)
    constexpr ManifestOs os = ManifestOs::MacOs;
#elif defined(__linux__)
    constexpr ManifestOs os = ManifestOs::Linux;
#else
#error "unsupported target OS"
#endif
#if defined(_M_X64) || defined(__x86_64__)
    constexpr ManifestArch arch = ManifestArch::X64;
#elif defined(_M_ARM64) || defined(__aarch64__)
    constexpr ManifestArch arch = ManifestArch::Arm64;
#else
#error "unsupported target architecture"
#endif
    return {os, arch};
}

}  // namespace rb::components
