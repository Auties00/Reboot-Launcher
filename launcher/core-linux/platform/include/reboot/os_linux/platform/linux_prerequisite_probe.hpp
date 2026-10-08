#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_linux::platform {

// Covers no capability ids; IPrerequisiteProbe for Linux play and unattended hosting.
class LinuxPrerequisiteProbe final : public ports::IPrerequisiteProbe {
public:
    // umu-run is a Python zipapp that runs on the host's interpreter.
    inline static constexpr std::string_view kPython3Id = "linux.python3";
    // DXVK and vkd3d inside the Steam Linux Runtime still load the host's libvulkan.so.1.
    inline static constexpr std::string_view kVulkanLoaderId = "linux.vulkan_loader";
    // Without linger, logging out stops the user manager and with it the engine and its hosts.
    inline static constexpr std::string_view kLingerId = "linux.linger";

    // `user_name` is the effective user's, from getpwuid_r.
    explicit LinuxPrerequisiteProbe(std::string user_name);

    // Python3: the first executable python3 on the engine's PATH, which sessions inherit and
    // where umu-run's `#!/usr/bin/env python3` finds it, runs `-c pass` within 5 s.
    // VulkanLoader: dlopen("libvulkan.so.1"). Linger: /var/lib/systemd/linger/<user> exists.
    std::vector<ports::PrerequisiteStatus> check() override;
    // Linger runs `loginctl enable-linger <user>`, which polkit allows for one's own user on
    // most distributions; the others fail with platform.no_remediation.
    Result<void> remediate(std::string_view id) override;

private:
    std::string user_name_;
};

}  // namespace reboot::os_linux::platform
