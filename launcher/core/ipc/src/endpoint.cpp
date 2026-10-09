#include "reboot/ipc/endpoint.hpp"

#include <algorithm>

#include "reboot/ipc/ipc_errors.hpp"

namespace reboot::ipc {

namespace {

constexpr std::size_t kMaxUserIdLength = 256;

[[nodiscard]] constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

[[nodiscard]] bool is_decimal(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, is_digit);
}

// "S-1-" then dash-separated decimal sub-authorities, as ConvertSidToStringSidW writes it.
[[nodiscard]] bool is_sid(std::string_view text) noexcept {
    if (!text.starts_with("S-1-")) return false;
    std::string_view rest = text.substr(4);
    while (true) {
        const std::size_t dash = rest.find('-');
        if (!is_decimal(rest.substr(0, dash))) return false;
        if (dash == std::string_view::npos) return true;
        rest = rest.substr(dash + 1);
    }
}

[[nodiscard]] Diagnostic invalid_input(std::string_view field) {
    return make_diag(ErrorDomain::Ipc, kInvalidEndpointInput).arg("field", field).kind(ErrorKind::InvalidInput);
}

}  // namespace

bool is_root_hash16(std::string_view text) noexcept {
    return text.size() == 16 &&
           std::ranges::all_of(text, [](char c) { return is_digit(c) || (c >= 'a' && c <= 'f'); });
}

Result<std::string> endpoint_for(const ports::PeerIdentity& user, std::string_view root_hash16) {
    const std::string_view user_id = user.user_id;
    if (user_id.size() > kMaxUserIdLength || !(is_decimal(user_id) || is_sid(user_id)))
        return std::unexpected(invalid_input("user_id"));
    if (!is_root_hash16(root_hash16)) return std::unexpected(invalid_input("root_hash16"));
    std::string name = ports::endpoint_name(user, root_hash16);
    // The POSIX modules return nothing when the per-user directory cannot be named for this user.
    if (name.empty()) return std::unexpected(invalid_input("user_id"));
    return name;
}

}  // namespace reboot::ipc
