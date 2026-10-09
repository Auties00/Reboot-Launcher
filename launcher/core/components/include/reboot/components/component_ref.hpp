#pragma once

#include <array>
#include <compare>
#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::components {

// The foundation has no named digest type; this matches contracts::winhost::InjectSpec::sha256.
using Sha256Digest = std::array<u8, 32>;

enum class ComponentKind : u8 { Payload, Runtime };

// The client DLL and winhost ship as one payload version; the game server is not a payload.
// Winhost runs only under Wine: on Windows the engine injects itself.
enum class PayloadRole : u8 { ClientDll, Winhost };

// VcRedist is the VC++ redistributable, a Windows PE seeded into every Wine prefix.
enum class RuntimeKind : u8 { MacWine, Umu, GeProton, KronWine, VcRedist };

// The component id of the payload; a runtime's id is its manifest id.
inline constexpr std::string_view kPayloadComponentId = "payload";

[[nodiscard]] constexpr std::string_view payload_file_name(PayloadRole role) noexcept {
    switch (role) {
        case PayloadRole::ClientDll: return "rb_client.dll";
        case PayloadRole::Winhost: return "reboot-winhost.exe";
    }
    return {};
}

// One version of one component. A payload's version is its SemVer text.
struct ComponentRef {
    ComponentKind kind{};
    std::string id;
    std::string version;

    auto operator<=>(const ComponentRef&) const = default;
};

}  // namespace rb::components
