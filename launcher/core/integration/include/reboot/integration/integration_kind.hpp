#pragma once

#include <array>

#include "reboot/ports/os_services.hpp"

namespace rb::integration {

// The port's enum, so state.json's declined flags and the registrar speak the same kinds.
using IntegrationKind = ports::IntegrationKind;

inline constexpr std::array<IntegrationKind, 4> kAllIntegrationKinds{
    IntegrationKind::UrlScheme, IntegrationKind::Autostart, IntegrationKind::EngineAgent,
    IntegrationKind::DesktopEntry};

}  // namespace rb::integration
