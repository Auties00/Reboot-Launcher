#include "reboot/testing/game_control_bootstrap.hpp"

#include <cstdlib>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "messages.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::testing {
namespace {

constexpr std::string_view kTcpScheme = "tcp://";

[[nodiscard]] Diagnostic bad(std::string_view name) {
    return make_diag(kTestingDomain, msg::kBadBootstrap).arg("name", name).kind(ErrorKind::InvalidInput);
}

[[nodiscard]] std::optional<std::string_view> lookup(const ports::EnvBlock& env, std::string_view name) {
    for (const auto& [key, value] : env.vars)
        if (key == name) return value;
    return std::nullopt;
}

[[nodiscard]] std::optional<u8> hex_digit(char c) {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
    return std::nullopt;
}

[[nodiscard]] std::optional<u8> base64url_digit(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<u8>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<u8>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0' + 52);
    if (c == '-') return u8{62};
    if (c == '_') return u8{63};
    return std::nullopt;
}

// 32 bytes are 256 bits: 42 full sextets and 4 bits of a 43rd, whose 2 low bits must be zero.
[[nodiscard]] std::optional<std::array<u8, 32>> parse_base64url_token(std::string_view text) {
    std::array<u8, 32> token{};
    if (text.size() != 43) return std::nullopt;
    u32 bits = 0;
    int held = 0;
    std::size_t out = 0;
    for (const char c : text) {
        const auto digit = base64url_digit(c);
        if (!digit) return std::nullopt;
        bits = bits << 6 | *digit;
        held += 6;
        if (held >= 8) {
            held -= 8;
            token[out++] = static_cast<u8>(bits >> held);
        }
    }
    if ((bits & ((1u << held) - 1)) != 0) return std::nullopt;
    return token;
}

[[nodiscard]] std::optional<std::array<u8, 32>> parse_token(std::string_view text) {
    std::array<u8, 32> token{};
    if (text.size() != token.size() * 2) return parse_base64url_token(text);
    for (std::size_t i = 0; i < token.size(); ++i) {
        const auto high = hex_digit(text[2 * i]);
        const auto low = hex_digit(text[2 * i + 1]);
        if (!high || !low) return std::nullopt;
        token[i] = static_cast<u8>(*high << 4 | *low);
    }
    return token;
}

[[nodiscard]] std::optional<Endpoint> parse_ctl(std::string_view text) {
    if (!text.starts_with(kTcpScheme)) return std::nullopt;
    const auto host_port = parse_host_port(text.substr(kTcpScheme.size()));
    if (!host_port || !host_port->port) return std::nullopt;
    const auto address = IpAddress::parse(host_port->host);
    if (!address) return std::nullopt;
    return Endpoint{*address, *host_port->port};
}

[[nodiscard]] std::optional<std::string> own_variable(std::string_view name) {
    const std::string key(name);
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, key.c_str()) != 0 || value == nullptr) return std::nullopt;
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) return std::nullopt;
    return std::string(value);
#endif
}

}  // namespace

Result<GameControlBootstrap> read_game_control_bootstrap(const ports::EnvBlock& env) {
    namespace gc = contracts::game_client;
    GameControlBootstrap bootstrap;

    const auto ctl = lookup(env, gc::kEnvCtl);
    const auto engine = ctl ? parse_ctl(*ctl) : std::nullopt;
    if (!engine) return std::unexpected(bad(gc::kEnvCtl));
    bootstrap.engine = *engine;

    const auto token_text = lookup(env, gc::kEnvCtlToken);
    const auto token = token_text ? parse_token(*token_text) : std::nullopt;
    if (!token) return std::unexpected(bad(gc::kEnvCtlToken));
    bootstrap.token = *token;

    const auto session_text = lookup(env, gc::kEnvSession);
    const auto session = session_text ? parse_uuid(*session_text) : Result<Uuid>(std::unexpected(bad(gc::kEnvSession)));
    if (!session) return std::unexpected(bad(gc::kEnvSession));
    bootstrap.session = SessionId{*session};

    const auto role = lookup(env, gc::kEnvRole);
    if (!role || role->empty()) return std::unexpected(bad(gc::kEnvRole));
    bootstrap.role = std::string(*role);
    return bootstrap;
}

Result<GameControlBootstrap> read_game_control_bootstrap(std::span<const u8> env_block_utf16) {
    ports::EnvBlock env;
    std::u16string entry;
    for (std::size_t i = 0; i + 1 < env_block_utf16.size(); i += 2) {
        const auto unit = static_cast<char16_t>(env_block_utf16[i] | env_block_utf16[i + 1] << 8);
        if (unit != u'\0') {
            entry.push_back(unit);
            continue;
        }
        if (entry.empty()) break;
        const std::string text = utf16_to_utf8(entry);
        entry.clear();
        // Skips the leading '=' of the per-drive "=C:=C:\..." entries.
        const std::size_t equals = text.find('=', 1);
        if (equals == std::string::npos) continue;
        env.vars.emplace_back(text.substr(0, equals), text.substr(equals + 1));
    }
    return read_game_control_bootstrap(env);
}

Result<GameControlBootstrap> read_own_game_control_bootstrap() {
    namespace gc = contracts::game_client;
    ports::EnvBlock env;
    for (const std::string_view name : {gc::kEnvCtl, gc::kEnvCtlToken, gc::kEnvSession, gc::kEnvRole})
        if (auto value = own_variable(name)) env.vars.emplace_back(std::string(name), std::move(*value));
    return read_game_control_bootstrap(env);
}

}  // namespace reboot::testing
