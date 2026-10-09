#pragma once

#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"
#include "socket_table.hpp"

namespace reboot::os_linux::platform {

// wineserver owns every socket of a Wine process; its exe or comm names it.
[[nodiscard]] bool is_wine_server(const NativePath& exe, std::string_view comm);

// The process holding socket `inode`, found through the socket:[inode] links in /proc/<pid>/fd.
// A socket no readable fd table holds (another user's) reports pid 0 and no exe.
[[nodiscard]] ports::PortOwner socket_owner(u64 inode);

// The sockets of a /proc/net table pair, such as tcp and tcp6; an unreadable file adds nothing.
[[nodiscard]] std::vector<SocketRecord> read_proc_net(std::string_view v4_name, std::string_view v6_name);

}  // namespace reboot::os_linux::platform
