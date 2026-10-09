#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/paths.hpp"
#include "reboot/gameserver/game_server_config.hpp"
#include "reboot/gameserver/game_server_description.hpp"
#include "reboot/gameserver/game_server_error.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/gameserver/operator_command.hpp"
#include "reboot/gameserver/socket_role.hpp"
#include "reboot/testing/fake_platform_paths.hpp"

using namespace reboot;
using namespace reboot::gameserver;
namespace gs = reboot::contracts::game_server;

namespace {

GameServerDescription two_sockets() {
    GameServerDescription description;
    description.protocol = gs::kGameServerProtocol;
    description.build = "1.0";
    description.sockets = {gs::SocketSpec{gs::SocketRole::Game}, gs::SocketSpec{gs::SocketRole::Beacon}};
    description.capabilities = gs::ServerCapabilities{false, false, {"start_match", "kick"}};
    return description;
}

GameServerConfig valid_config() {
    GameServerConfig config;
    config.game.version = *GameVersion::parse("14.40");
    config.game.cl = Changelist{14550713};
    config.listen.ports = {Port{7777}, Port{7778}};
    config.match.playlist = "Playlist_DefaultSolo";
    config.match.max_players = 100;
    config.match.tick_rate = 30;
    config.match.start = AutoAtPlayers{2};
    config.operator_cidrs = {"192.168.1.0/24", "::1"};
    config.bans = {Ban{"203.0.113.7", std::string("abc"), std::nullopt, "cheating"}};
    return config;
}

std::string error_id(const Result<void>& result) { return result ? std::string() : result.error().id; }

const Arg* arg(const Diagnostic& diag, std::string_view name) { return diag.find_arg(name); }

}  // namespace

TEST_CASE("a valid config passes", "[gameserver][config]") {
    CHECK(validate(valid_config(), two_sockets()).has_value());
}

TEST_CASE("the port block must match the declared sockets", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.listen.ports = {Port{7777}};
    const Result<void> result = validate(config, two_sockets());
    REQUIRE(error_id(result) == "gameserver.port_count_mismatch");
    CHECK(*arg(result.error(), "expected") == Arg{u64{2}});
    CHECK(*arg(result.error(), "actual") == Arg{u64{1}});
    CHECK(result.error().kind == ErrorKind::InvalidInput);
}

TEST_CASE("a zero or repeated port is refused", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.listen.ports = {Port{0}, Port{7778}};
    CHECK(error_id(validate(config, two_sockets())) == "gameserver.invalid_port");
    config.listen.ports = {Port{7777}, Port{7777}};
    const Result<void> repeated = validate(config, two_sockets());
    REQUIRE(error_id(repeated) == "gameserver.invalid_port");
    CHECK(*arg(repeated.error(), "port") == Arg{u64{7777}});
}

TEST_CASE("the server binds IPv4 only", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.listen.bind_address = *IpAddress::parse("::");
    const Result<void> result = validate(config, two_sockets());
    REQUIRE(error_id(result) == "gameserver.bind_not_ipv4");
    CHECK(*arg(result.error(), "address") == Arg{std::string("::")});
}

TEST_CASE("a server that needs a backend gets one", "[gameserver][config]") {
    GameServerDescription description = two_sockets();
    description.capabilities.needs_backend = true;
    GameServerConfig config = valid_config();
    CHECK(error_id(validate(config, description)) == "gameserver.backend_required");
    config.backend = BackendAccess{"http://127.0.0.1:3551", "host", SecretString(std::string("token"))};
    CHECK(validate(config, description).has_value());
}

TEST_CASE("match settings are range checked", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.match.start = AutoAtPlayers{0};
    Result<void> result = validate(config, two_sockets());
    REQUIRE(error_id(result) == "gameserver.invalid_match_setting");
    CHECK(*arg(result.error(), "field") == Arg{std::string("start")});

    config.match.start = AutoAtPlayers{101};
    CHECK(error_id(validate(config, two_sockets())) == "gameserver.invalid_match_setting");

    // Zero max players is the server's own default, so any start count goes.
    config.match.max_players = 0;
    CHECK(validate(config, two_sockets()).has_value());

    config.match.playlist = std::string("\xff\xfe");
    result = validate(config, two_sockets());
    REQUIRE(error_id(result) == "gameserver.invalid_match_setting");
    CHECK(*arg(result.error(), "field") == Arg{std::string("playlist")});
}

TEST_CASE("operator and ban addresses must be IPs or CIDR blocks", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.operator_cidrs = {"10.0.0.0/33"};
    Result<void> result = validate(config, two_sockets());
    REQUIRE(error_id(result) == "gameserver.invalid_address");
    CHECK(*arg(result.error(), "address") == Arg{std::string("10.0.0.0/33")});

    config = valid_config();
    config.operator_cidrs = {"fe80::/64", "10.1.2.3/8"};
    CHECK(validate(config, two_sockets()).has_value());

    config.bans = {Ban{"", std::string("account"), std::nullopt, ""}};
    CHECK(error_id(validate(config, two_sockets())) == "gameserver.invalid_address");
    config.bans = {Ban{"example.com", std::nullopt, std::nullopt, ""}};
    CHECK(error_id(validate(config, two_sockets())) == "gameserver.invalid_address");
}

TEST_CASE("a build root must be absolute", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.game.build_root = NativePath("relative/build");
    CHECK(error_id(validate(config, two_sockets())) == "gameserver.path_not_absolute");
    config.game.build_root = testing::default_fake_root() / "builds" / "14.40";
    CHECK(validate(config, two_sockets()).has_value());
}

TEST_CASE("to_wire fills the ServerConfig", "[gameserver][config]") {
    GameServerConfig config = valid_config();
    config.backend = BackendAccess{"http://127.0.0.1:3551", "host-account", SecretString(std::string("s3cret"))};
    config.listen.bind_address = IpAddress::v4(0x7F000001);
    const SessionId session{*parse_uuid("9a3e7c51-2d44-4c1b-8f0a-61b2d5e9c3f7")};
    const NativePath log_dir = testing::default_fake_root() / "logs";

    const gs::ServerConfig wire = to_wire(config, session, log_dir);
    CHECK(wire.session_id == session.value);
    CHECK(wire.game.version == "14.40");
    CHECK(wire.game.cl == 14550713);
    CHECK_FALSE(wire.game.build_root.has_value());
    CHECK(wire.listen.bind_address == "127.0.0.1");
    CHECK(wire.listen.ports == std::vector<u16>{7777, 7778});
    REQUIRE(wire.backend.has_value());
    CHECK(wire.backend->origin == "http://127.0.0.1:3551");
    CHECK(wire.backend->account_id == "host-account");
    CHECK(wire.backend->service_token == "s3cret");
    CHECK(wire.match.playlist == "Playlist_DefaultSolo");
    CHECK(wire.match.start_policy == gs::StartPolicy::AutoAtPlayers);
    CHECK(wire.match.start_at_players == 2);
    CHECK(wire.match.max_players == 100);
    CHECK(wire.match.tick_rate == 30);
    CHECK(wire.operators.ip_cidrs == config.operator_cidrs);
    REQUIRE(wire.bans.size() == 1);
    CHECK(wire.bans[0].address == "203.0.113.7");
    CHECK(wire.log_dir == to_wire(log_dir));

    config.match.start = ManualStart{};
    config.backend.reset();
    config.game.build_root = testing::default_fake_root() / "build";
    const gs::ServerConfig manual = to_wire(config, session, log_dir);
    CHECK(manual.match.start_policy == gs::StartPolicy::Manual);
    CHECK(manual.match.start_at_players == 0);
    CHECK_FALSE(manual.backend.has_value());
    REQUIRE(manual.game.build_root.has_value());
    CHECK(*manual.game.build_root == to_wire(*config.game.build_root));
}

TEST_CASE("same_description compares every field", "[gameserver][description]") {
    const GameServerDescription a = two_sockets();
    GameServerDescription b = two_sockets();
    CHECK(same_description(a, b));
    b.capabilities.operator_commands = {"kick", "start_match"};
    CHECK_FALSE(same_description(a, b));
    b = two_sockets();
    b.supports = {gs::VersionSupport{"1.0", "2.0", {}}};
    CHECK_FALSE(same_description(a, b));
}

TEST_CASE("socket_roles follows declaration order", "[gameserver][description]") {
    CHECK(socket_roles(two_sockets()) == std::vector<SocketRole>{SocketRole::Game, SocketRole::Beacon});
    CHECK(socket_roles(GameServerDescription{}).empty());
}

TEST_CASE("is_declared reads the capabilities", "[gameserver][commands]") {
    GameServerCapabilities capabilities{false, false, {"start_match", "kick", "reset"}};
    CHECK(is_declared(StartMatch{}, capabilities));
    CHECK(is_declared(Kick{1, "bye"}, capabilities));
    CHECK_FALSE(is_declared(EndMatch{}, capabilities));
    // "reset" in the list does not count; only in_process_reset does.
    CHECK_FALSE(is_declared(ResetMatch{}, capabilities));
    capabilities.in_process_reset = true;
    CHECK(is_declared(ResetMatch{}, capabilities));
    CHECK(command_name(Drain{}) == "drain");
    CHECK(command_name(SetOperators{}) == "set_operators");
}

TEST_CASE("every error code converts with its arguments", "[gameserver][errors]") {
    const NativePath exe = testing::default_fake_root() / "app" / "reboot-game-server";
    const Diagnostic timeout = to_diagnostic(GameServerError{.code = GameServerErrorCode::DescribeTimeout,
                                                             .path = exe,
                                                             .timeout = std::chrono::seconds{10}});
    CHECK(timeout.id == "gameserver.describe_timeout");
    CHECK(timeout.domain == ErrorDomain::GameServer);
    CHECK(timeout.retryable);
    CHECK(*timeout.find_arg("timeout") == Arg{std::chrono::milliseconds{10000}});
    CHECK(*timeout.find_arg("path") == Arg{to_wire(exe)});

    const Diagnostic cause = make_diag(ErrorDomain::Process, MessageId{"process.child_gone"}).build();
    const Diagnostic dir = to_diagnostic(
        GameServerError{.code = GameServerErrorCode::SessionDirFailed, .path = exe, .cause = cause});
    REQUIRE(dir.causes.size() == 1);
    CHECK(dir.causes[0].id == "process.child_gone");

    const Diagnostic command = to_diagnostic(GameServerError{.code = GameServerErrorCode::CommandNotDeclared, .command = "kick"});
    CHECK(command.id == "gameserver.command_not_declared");
    CHECK(*command.find_arg("command") == Arg{std::string("kick")});
    CHECK(command.kind == ErrorKind::Unsupported);

    for (u8 code = 0; code <= static_cast<u8>(GameServerErrorCode::CommandTimeout); ++code) {
        const Diagnostic diag = to_diagnostic(GameServerError{.code = static_cast<GameServerErrorCode>(code)});
        CHECK(diag.id.starts_with("gameserver."));
    }
}

TEST_CASE("locate_game_server prefers the dev override and needs an absolute path", "[gameserver][binary]") {
    InstallLayout install;
    install.game_server_exe = testing::default_fake_root() / "app" / "reboot-game-server";
    Result<NativePath> bundled = locate_game_server(install, std::nullopt);
    REQUIRE(bundled.has_value());
    CHECK(*bundled == install.game_server_exe);

    const NativePath dev = testing::default_fake_root() / "build" / "reboot-game-server";
    Result<NativePath> overridden = locate_game_server(install, dev);
    REQUIRE(overridden.has_value());
    CHECK(*overridden == dev);

    Result<NativePath> relative = locate_game_server(install, NativePath("build/reboot-game-server"));
    REQUIRE_FALSE(relative.has_value());
    CHECK(relative.error().id == "gameserver.path_not_absolute");
}
