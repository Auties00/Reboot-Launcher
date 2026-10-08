#include "reboot/game_channel/lifecycle_markers.hpp"

#include <algorithm>
#include <array>

namespace reboot::game_channel {
namespace {

// 10.0.9's constants (common/lib/src/constant/game.dart); its one "cannot connect" list is split
// into AuthFailure and CannotConnect.
constexpr std::array<std::string_view, 1> kShutdown{"FOnlineSubsystemGoogleCommon::Shutdown()"};
constexpr std::array<std::string_view, 1> kCriticalError{"Critical error"};
constexpr std::array<std::string_view, 1> kTruncatedRead{"when 0 bytes remain"};
constexpr std::array<std::string_view, 1> kPakSignature{"Pak chunk signature verification failed!"};
constexpr std::array<std::string_view, 1> kFatalError{"LogWindows:Error: Fatal error!"};
constexpr std::array<std::string_view, 1> kLoginRefused{"Unable to login to Fortnite servers"};
constexpr std::array<std::string_view, 1> kHttp400{"HTTP 400 response from "};
constexpr std::array<std::string_view, 1> kForceLogout{"UOnlineAccountCommon::ForceLogout"};
constexpr std::array<std::string_view, 1> kBackendRefused{"port 3551 failed: Connection refused"};
constexpr std::array<std::string_view, 1> kPlatformCheck{
    "Network failure when attempting to check platform restrictions"};
constexpr std::array<std::string_view, 2> kLoginCompleted{"[UOnlineAccountCommon::ContinueLoggingIn]", "(Completed)"};

constexpr std::array<MarkerPattern, 11> kBuiltinPatterns{{
    {LegacyMarker::Shutdown, kShutdown},
    {LegacyMarker::CorruptBuild, kCriticalError},
    {LegacyMarker::CorruptBuild, kTruncatedRead},
    {LegacyMarker::CorruptBuild, kPakSignature},
    {LegacyMarker::CorruptBuild, kFatalError},
    {LegacyMarker::AuthFailure, kLoginRefused},
    {LegacyMarker::AuthFailure, kHttp400},
    {LegacyMarker::AuthFailure, kForceLogout},
    {LegacyMarker::CannotConnect, kBackendRefused},
    {LegacyMarker::CannotConnect, kPlatformCheck},
    {LegacyMarker::LoginCompleted, kLoginCompleted},
}};

constexpr LifecycleMarkers kBuiltinMarkers{1, kBuiltinPatterns};

}  // namespace

std::optional<LegacyMarker> LifecycleMarkers::match(std::string_view line) const noexcept {
    for (const MarkerPattern& pattern : patterns) {
        const bool all_found = !pattern.substrings.empty() &&
                               std::ranges::all_of(pattern.substrings, [line](std::string_view part) {
                                   return line.find(part) != std::string_view::npos;
                               });
        if (all_found) return pattern.marker;
    }
    return std::nullopt;
}

const LifecycleMarkers& builtin_lifecycle_markers() noexcept { return kBuiltinMarkers; }

}  // namespace reboot::game_channel
