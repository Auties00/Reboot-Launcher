#pragma once

#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::builds {

// Capabilities: game-builds.download-lifecycle.
// Only the op's outcome means done, never a percentage. Verifying and Extracting report progress
// per chunk, so the Install liveness deadline holds over an 85 GB archive.
enum class InstallPhase : u8 { Preparing, Downloading, Verifying, Extracting, Detecting, Registering, CleaningUp };

// The Progress::phase literal of each phase.
[[nodiscard]] constexpr std::string_view install_phase_name(InstallPhase phase) noexcept {
    switch (phase) {
        case InstallPhase::Preparing: return "preparing";
        case InstallPhase::Downloading: return "downloading";
        case InstallPhase::Verifying: return "verifying";
        case InstallPhase::Extracting: return "extracting";
        case InstallPhase::Detecting: return "detecting";
        case InstallPhase::Registering: return "registering";
        case InstallPhase::CleaningUp: return "cleaning_up";
    }
    return "preparing";
}

}  // namespace rb::builds
