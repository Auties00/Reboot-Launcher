#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IPrerequisiteProbe for play under Wine and for hosting. Blocking.
class MacPrerequisiteProbe final : public ports::IPrerequisiteProbe {
public:
    inline static constexpr std::string_view kAppleSiliconId = "mac.apple_silicon";
    inline static constexpr std::string_view kMinimumVersionId = "mac.os_min_14";
    inline static constexpr std::string_view kRosettaId = "mac.rosetta";
    inline static constexpr std::string_view kMetal3Id = "mac.metal3";
    inline static constexpr std::string_view kAppFirewallId = "mac.app_firewall_state";
    inline static constexpr std::string_view kLocalNetworkId = "mac.local_network_hint";
    inline static constexpr u32 kMinimumMajorVersion = 14;

    // Nullopt in a dev build; the firewall check then sees only whether all incoming is blocked.
    explicit MacPrerequisiteProbe(std::optional<NativePath> game_server_exe);

    // Never sends anything: local_network_hint reports the last remediate() probe, met before one.
    std::vector<ports::PrerequisiteStatus> check() override;
    // local_network_hint sends a gateway datagram; a process's first send only raises the prompt.
    // Rosetta installs unelevated.
    Result<void> remediate(std::string_view id) override;

private:
    std::optional<NativePath> game_server_exe_;
    bool local_network_prompted_ = false;
    bool local_network_denied_ = false;
};

}  // namespace reboot::os_macos::platform
