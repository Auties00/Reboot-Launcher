#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::net {

// System is pid 0 or 4 on Windows, such as http.sys holding :80. WineHost is wineserver holding
// the port for a Windows process. Unknown is an owner that could not be classified.
enum class PortOwnerClass : u8 { Ours, Foreign, System, WineHost, Unknown };

struct PortOwnerInfo {
    PortOwnerClass owner_class = PortOwnerClass::Unknown;
    ports::PortOwner owner;
    // Why this owner is Unknown, when inspecting its process failed.
    std::optional<Diagnostic> lookup_error;
};

}  // namespace reboot::net
