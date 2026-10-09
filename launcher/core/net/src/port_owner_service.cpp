#include "reboot/net/port_owner_service.hpp"

#include <optional>
#include <utility>

#include "reboot/ports/net.hpp"
#include "reboot/ports/process.hpp"

namespace reboot::net {

namespace {

// Elsewhere pid 0 is an owner the inspector could not name, such as another user's process.
[[nodiscard]] bool is_system_pid(u32 pid) noexcept {
#ifdef _WIN32
    return pid == 0 || pid == 4;
#else
    (void)pid;
    return false;
#endif
}

}  // namespace

Result<std::vector<PortOwnerInfo>> PortOwnerService::owners(PortProtocol protocol, Endpoint local,
                                                             std::span<const OurProcess> ours) {
    Result<std::optional<ports::PortOwner>> found =
        protocol == PortProtocol::Udp ? inspector_.udp_owner(local.port) : inspector_.tcp_owner(local);
    if (!found) return std::unexpected(std::move(found.error()));
    std::vector<PortOwnerInfo> out;
    if (!*found) return out;

    PortOwnerInfo info;
    info.owner = std::move(**found);
    if (info.owner.wine_server) {
        info.owner_class = PortOwnerClass::WineHost;
    } else if (is_system_pid(info.owner.pid)) {
        info.owner_class = PortOwnerClass::System;
    } else if (info.owner.pid == 0) {
        info.owner_class = PortOwnerClass::Unknown;
    } else {
        info.owner_class = PortOwnerClass::Foreign;
        for (const OurProcess& process : ours) {
            if (process.pid != info.owner.pid) continue;
            Result<bool> alive = processes_.is_alive(process.pid, process.created);
            if (!alive) {
                info.owner_class = PortOwnerClass::Unknown;
                info.lookup_error = std::move(alive.error());
            } else if (*alive) {
                info.owner_class = PortOwnerClass::Ours;
            }
            break;
        }
    }
    out.push_back(std::move(info));
    return out;
}

Result<bool> PortOwnerService::held_by(PortProtocol protocol, Endpoint local, const OurProcess& process) {
    Result<std::vector<PortOwnerInfo>> found = owners(protocol, local, std::span(&process, 1));
    if (!found) return std::unexpected(std::move(found.error()));
    for (const PortOwnerInfo& owner : *found)
        if (owner.owner_class == PortOwnerClass::Ours && owner.owner.pid == process.pid) return true;
    return false;
}

}  // namespace reboot::net
