#include "field_checks.hpp"

#include <algorithm>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/publish/field_limits.hpp"

namespace rb::publish {

std::string fit_text(std::string_view text, std::size_t max_bytes) {
    std::string out = sanitize_display_text(text);
    // sanitize_display_text keeps tabs, but the edge refuses every control character.
    std::ranges::replace(out, '\t', ' ');
    if (out.size() <= max_bytes) return out;
    std::size_t end = max_bytes;
    while (end > 0 && (static_cast<u8>(out[end]) & 0xC0) == 0x80) --end;
    out.resize(end);
    return out;
}

Result<std::string> fit_server_name(std::string_view name) {
    std::string out = fit_text(name, kMaxServerNameBytes);
    if (out.empty()) return make_diag(ErrorDomain::Publish, msg::kServerNameEmpty).kind(ErrorKind::InvalidInput).fail();
    return out;
}

Result<void> check_max_players(u32 max_players) {
    if (max_players > kMaxPlayerLimit)
        return make_diag(ErrorDomain::Publish, msg::kMaxPlayersTooHigh)
            .kind(ErrorKind::InvalidInput)
            .arg("max", kMaxPlayerLimit)
            .fail();
    return {};
}

Result<void> check_player_count(u32 players) {
    if (players > kMaxPlayerLimit)
        return make_diag(ErrorDomain::Publish, msg::kPlayerCountTooHigh)
            .kind(ErrorKind::InvalidInput)
            .arg("max", kMaxPlayerLimit)
            .fail();
    return {};
}

Result<void> check_password(const SecretString& password) {
    if (password.reveal().size() > kMaxPasswordBytes)
        return make_diag(ErrorDomain::Publish, msg::kPasswordTooLong)
            .kind(ErrorKind::InvalidInput)
            .arg("max", kMaxPasswordBytes)
            .fail();
    return {};
}

Result<void> check_game_port(Port port) {
    if (port.value == 0)
        return make_diag(ErrorDomain::Publish, msg::kGamePortMissing).kind(ErrorKind::InvalidInput).fail();
    return {};
}

}  // namespace rb::publish
