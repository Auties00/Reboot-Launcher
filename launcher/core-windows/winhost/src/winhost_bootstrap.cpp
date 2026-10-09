#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/winhost/winhost_bootstrap.hpp"

#include <charconv>
#include <string>
#include <utility>

#include "reboot/contracts/game_client.hpp"

namespace reboot::os_windows::winhost {
namespace {

constexpr std::string_view kCtlPrefix = "tcp://127.0.0.1:";
constexpr std::size_t kTokenChars = 43;

std::optional<u8> base64url_value(char c) noexcept {
    if (c >= 'A' && c <= 'Z') return static_cast<u8>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<u8>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0' + 52);
    if (c == '-') return u8{62};
    if (c == '_') return u8{63};
    return std::nullopt;
}

// The variable's value as ASCII, wiping every intermediate copy; nullopt when it is unset or not
// ASCII. Always removes the variable, so a companion never inherits it.
std::optional<std::string> take_variable(std::string_view name) {
    const std::wstring wide_name(name.begin(), name.end());
    std::wstring value(64, L'\0');
    DWORD length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {
        // Too small: `length` is the size needed, terminator included.
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.assign(length, L'\0');
        length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), static_cast<DWORD>(value.size()));
    }
    SetEnvironmentVariableW(wide_name.c_str(), nullptr);

    std::optional<std::string> out;
    if (length > 0 && length < value.size()) {
        std::string text(length, '\0');
        bool ascii = true;
        for (DWORD i = 0; i < length; ++i) {
            ascii = ascii && value[i] < 0x80;
            text[i] = static_cast<char>(value[i] & 0x7F);
        }
        if (ascii) out = std::move(text);
        else secure_wipe(text.data(), text.size());
    }
    SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
    return out;
}

}  // namespace

std::optional<u16> parse_ctl_endpoint(std::string_view value) noexcept {
    if (!value.starts_with(kCtlPrefix)) return std::nullopt;
    const std::string_view port_text = value.substr(kCtlPrefix.size());
    // from_chars would take a leading '-' for a signed type and stop at any trailing junk.
    if (port_text.empty() || port_text.size() > 5 || port_text.front() < '0' || port_text.front() > '9')
        return std::nullopt;
    unsigned port = 0;
    const auto [end, ec] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (ec != std::errc{} || end != port_text.data() + port_text.size() || port == 0 || port > 65535)
        return std::nullopt;
    return static_cast<u16>(port);
}

std::optional<ControlTokenBytes> decode_control_token(std::string_view value) {
    if (value.size() != kTokenChars) return std::nullopt;
    std::array<u8, kControlTokenSize> bytes{};
    u32 bits = 0;
    int pending = 0;
    std::size_t out = 0;
    bool valid = true;
    for (const char c : value) {
        const auto v = base64url_value(c);
        if (!v) {
            valid = false;
            break;
        }
        bits = (bits << 6) | *v;
        pending += 6;
        if (pending >= 8) {
            pending -= 8;
            bytes[out++] = static_cast<u8>(bits >> pending);
            bits &= (u32{1} << pending) - 1;
        }
    }
    // 43 characters carry 258 bits; the 2 left over must be zero for the encoding to be canonical.
    valid = valid && out == kControlTokenSize && bits == 0;
    std::optional<ControlTokenBytes> token;
    if (valid) token.emplace(bytes);
    secure_wipe(bytes.data(), bytes.size());
    return token;
}

Expected<WinhostBootstrap> read_bootstrap() {
    namespace gc = contracts::game_client;
    auto ctl = take_variable(gc::kEnvCtl);
    auto token_text = take_variable(gc::kEnvCtlToken);

    const auto port = ctl ? parse_ctl_endpoint(*ctl) : std::nullopt;
    auto token = token_text ? decode_control_token(*token_text) : std::nullopt;
    if (token_text) secure_wipe(token_text->data(), token_text->size());
    if (!port || !token) return std::unexpected(WinhostFailure{FailureStep::Bootstrap, std::nullopt});
    return WinhostBootstrap{*port, std::move(*token)};
}

}  // namespace reboot::os_windows::winhost
