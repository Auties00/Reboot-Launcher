#include "address_text.hpp"

#include <charconv>
#include <system_error>

#include "reboot/foundation/net_types.hpp"

namespace reboot::gameserver {

bool is_ip_or_cidr(std::string_view text) {
    const std::size_t slash = text.find('/');
    const std::optional<IpAddress> address = IpAddress::parse(text.substr(0, slash));
    if (!address) return false;
    if (slash == std::string_view::npos) return true;
    const std::string_view digits = text.substr(slash + 1);
    if (digits.empty() || digits.size() > 3) return false;
    unsigned prefix = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), prefix);
    if (ec != std::errc{} || end != digits.data() + digits.size()) return false;
    return prefix <= (address->is_v4() ? 32u : 128u);
}

}  // namespace reboot::gameserver
