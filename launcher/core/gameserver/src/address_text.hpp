#pragma once

#include <string_view>

namespace reboot::gameserver {

// "a.b.c.d", "a.b.c.d/n", an IPv6 literal or "v6/n", with n within the address family.
[[nodiscard]] bool is_ip_or_cidr(std::string_view text);

}  // namespace reboot::gameserver
