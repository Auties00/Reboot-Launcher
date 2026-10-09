#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::publish {

// sanitize_display_text with tabs as spaces, then cut to `max_bytes` on a code point boundary.
[[nodiscard]] std::string fit_text(std::string_view text, std::size_t max_bytes);
// fit_text to kMaxServerNameBytes; publish.server_name_empty when nothing is left.
[[nodiscard]] Result<std::string> fit_server_name(std::string_view name);

[[nodiscard]] Result<void> check_max_players(u32 max_players);
[[nodiscard]] Result<void> check_player_count(u32 players);
[[nodiscard]] Result<void> check_password(const SecretString& password);
[[nodiscard]] Result<void> check_game_port(Port port);

}  // namespace rb::publish
