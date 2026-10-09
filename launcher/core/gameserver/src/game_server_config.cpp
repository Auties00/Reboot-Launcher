#include "reboot/gameserver/game_server_config.hpp"

#include <algorithm>
#include <utility>

#include "address_text.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/gameserver/game_server_error.hpp"

namespace rb::gameserver {

namespace {

namespace gs = contracts::game_server;

[[nodiscard]] std::unexpected<Diagnostic> fail(GameServerError error) {
    return std::unexpected(to_diagnostic(error));
}

[[nodiscard]] Result<void> validate_match(const MatchSettings& match) {
    const auto invalid = [](std::string field) {
        return fail({.code = GameServerErrorCode::InvalidMatchSetting, .field = std::move(field)});
    };
    if (!is_valid_utf8(match.playlist)) return invalid("playlist");
    if (const auto* automatic = std::get_if<AutoAtPlayers>(&match.start)) {
        if (automatic->players == 0) return invalid("start");
        if (match.max_players != 0 && automatic->players > match.max_players) return invalid("start");
    }
    return {};
}

}  // namespace

Result<void> validate(const GameServerConfig& config, const GameServerDescription& description) {
    const std::vector<Port>& ports = config.listen.ports;
    if (ports.size() != description.sockets.size())
        return fail({.code = GameServerErrorCode::PortCountMismatch,
                     .expected = description.sockets.size(),
                     .actual = ports.size()});
    for (std::size_t i = 0; i < ports.size(); ++i)
        if (ports[i].value == 0 || std::ranges::find(ports.begin(), ports.begin() + static_cast<std::ptrdiff_t>(i),
                                                     ports[i]) != ports.begin() + static_cast<std::ptrdiff_t>(i))
            return fail({.code = GameServerErrorCode::InvalidPort, .port = ports[i]});
    if (!config.listen.bind_address.is_v4())
        return fail({.code = GameServerErrorCode::BindNotIpv4, .address = config.listen.bind_address.to_string()});
    if (description.capabilities.needs_backend && !config.backend)
        return fail({.code = GameServerErrorCode::BackendRequired});
    if (Result<void> match = validate_match(config.match); !match) return match;
    for (const std::string& cidr : config.operator_cidrs)
        if (!is_ip_or_cidr(cidr)) return fail({.code = GameServerErrorCode::InvalidAddress, .address = cidr});
    for (const Ban& ban : config.bans)
        if (!is_ip_or_cidr(ban.address))
            return fail({.code = GameServerErrorCode::InvalidAddress, .address = ban.address});
    if (config.game.build_root && !config.game.build_root->is_absolute())
        return fail({.code = GameServerErrorCode::PathNotAbsolute, .path = *config.game.build_root});
    return {};
}

gs::ServerConfig to_wire(const GameServerConfig& config, const SessionId& session, const NativePath& log_dir) {
    gs::ServerConfig wire;
    wire.session_id = session.value;
    wire.game.version = config.game.version.canonical();
    wire.game.cl = config.game.cl.value;
    if (config.game.build_root) wire.game.build_root = rb::to_wire(*config.game.build_root);
    wire.listen.bind_address = config.listen.bind_address.to_string();
    wire.listen.ports.reserve(config.listen.ports.size());
    for (const Port port : config.listen.ports) wire.listen.ports.push_back(port.value);
    if (config.backend)
        wire.backend = gs::BackendLink{config.backend->origin, config.backend->account_id,
                                       config.backend->service_token.reveal()};
    wire.match.playlist = config.match.playlist;
    if (const auto* automatic = std::get_if<AutoAtPlayers>(&config.match.start)) {
        wire.match.start_policy = gs::StartPolicy::AutoAtPlayers;
        wire.match.start_at_players = automatic->players;
    } else {
        wire.match.start_policy = gs::StartPolicy::Manual;
    }
    wire.match.max_players = config.match.max_players;
    wire.match.tick_rate = config.match.tick_rate;
    wire.operators.ip_cidrs = config.operator_cidrs;
    wire.bans = config.bans;
    wire.log_dir = rb::to_wire(log_dir);
    return wire;
}

}  // namespace rb::gameserver
