#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/port_owner_info.hpp"
#include "reboot/net/port_protocol.hpp"

namespace reboot::net {

// InUse is EADDRINUSE; AccessDenied is EACCES (a reserved or excluded port range).
enum class PortConflictKind : u8 { InUse, AccessDenied };

// Becomes net.port_busy{port, protocol, owner, owned_by_us}, net.port_held_by_system or
// net.port_access_denied.
struct PortConflict {
    PortConflictKind kind = PortConflictKind::InUse;
    PortProtocol protocol = PortProtocol::Udp;
    Endpoint bind;
    std::vector<PortOwnerInfo> owners;
    // Set when the socket table could not be read, so `owners` is empty.
    std::optional<Diagnostic> lookup_error;

    [[nodiscard]] bool owned_by_us() const noexcept {
        for (const PortOwnerInfo& owner : owners)
            if (owner.owner_class == PortOwnerClass::Ours) return true;
        return false;
    }
};

[[nodiscard]] Diagnostic to_diagnostic(const PortConflict& conflict);

}  // namespace reboot::net
