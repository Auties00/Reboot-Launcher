#pragma once

#include <array>
#include <string_view>
#include <type_traits>

#include "reboot/contracts/backend.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::storage {

// Enumerators are stored by name, never by index: kNames[i] names the enumerator whose value is i.
template <class E>
struct EnumNames;

template <class E>
concept PersistedEnum = std::is_enum_v<E> && requires { EnumNames<E>::kNames; };

// Enums of other packages are named here, so every translation unit sees the same specialisations.
template <>
struct EnumNames<contracts::ipc::ClientKind> {
    static constexpr std::array<std::string_view, 6> kNames{"unknown", "windows_gui", "mac_gui", "linux_gui", "cli",
                                                            "test"};
};

template <>
struct EnumNames<contracts::ipc::EngineOrigin> {
    static constexpr std::array<std::string_view, 3> kNames{"on_demand", "service_manager", "foreground"};
};

template <>
struct EnumNames<contracts::backend::AccountRole> {
    static constexpr std::array<std::string_view, 2> kNames{"client", "host"};
};

template <>
struct EnumNames<ports::IntegrationKind> {
    static constexpr std::array<std::string_view, 4> kNames{"url_scheme", "autostart", "engine_agent",
                                                            "desktop_entry"};
};

}  // namespace rb::storage
