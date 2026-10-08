#pragma once

#include <charconv>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/types.hpp"

// Private wire messages. Field number = declaration index + 1, so fields are only appended.
// A run of std::optional fields documented as a choice has at most one member set.
namespace reboot::contracts {

struct FrameRange {
    u64 first;
    u64 last;

    [[nodiscard]] constexpr bool contains(u64 type) const noexcept { return type >= first && type <= last; }
};

inline constexpr FrameRange kCommonFrames{0x10, 0x1F};
inline constexpr FrameRange kIpcFrames{0x100, 0x1FF};
inline constexpr FrameRange kBackendFrames{0x200, 0x2FF};
inline constexpr FrameRange kGameServerFrames{0x300, 0x3FF};
inline constexpr FrameRange kGameClientFrames{0x400, 0x4FF};
inline constexpr FrameRange kWinhostFrames{0x500, 0x5FF};

template <class... T>
[[nodiscard]] constexpr bool at_most_one(const std::optional<T>&... members) noexcept {
    return (static_cast<int>(members.has_value()) + ... + 0) <= 1;
}

}  // namespace reboot::contracts

namespace reboot::contracts::common {

struct Ping {
    u64 nonce = 0;
};
REBOOT_CONTRACT_FRAME(Ping, 0x10)

struct Pong {
    u64 nonce = 0;
};
REBOOT_CONTRACT_FRAME(Pong, 0x11)

// Mirrors the alternatives of reboot::Arg, in order.
enum class ArgKind : u8 { String, Signed, Unsigned, Bool, Millis, Path, SemVer };

struct WireArg {
    std::string name;
    ArgKind kind{};
    std::string value;
};

struct WireDiagnostic {
    std::string id;
    std::vector<WireArg> args;
    std::optional<std::string> detail;
    SystemError::Origin os_origin{};
    std::optional<i64> os_code;
    bool retryable = false;
    ErrorKind kind{};
};

struct CommandResult {
    u64 req_id = 0;
    bool ok = false;
    std::optional<WireDiagnostic> error;
};
REBOOT_CONTRACT_FRAME(CommandResult, 0x12)

struct Unsupported {
    u64 req_id = 0;
};
REBOOT_CONTRACT_FRAME(Unsupported, 0x13)

// `category` is the sender's own; the engine logs it under the channel's LogCategory.
struct Log {
    LogLevel level{};
    u8 category = 0;
    std::string text;
};
REBOOT_CONTRACT_FRAME(Log, 0x14)

namespace detail {

[[nodiscard]] inline std::string arg_text(const Arg& arg) {
    return std::visit(
        []<class V>(const V& value) -> std::string {
            if constexpr (std::is_same_v<V, std::string>) {
                return value;
            } else if constexpr (std::is_same_v<V, bool>) {
                return value ? "1" : "0";
            } else if constexpr (std::is_same_v<V, std::chrono::milliseconds>) {
                return std::to_string(value.count());
            } else if constexpr (std::is_same_v<V, WirePath>) {
                return value.display;
            } else if constexpr (std::is_same_v<V, SemVer>) {
                std::string text = std::to_string(value.major) + '.' + std::to_string(value.minor) + '.' +
                                   std::to_string(value.patch);
                if (!value.pre.empty()) text += '-' + value.pre;
                return text;
            } else {
                return std::to_string(value);
            }
        },
        arg);
}

template <class N>
[[nodiscard]] std::optional<N> parse_number(std::string_view text) {
    N out{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return out;
}

// A value that does not parse as its kind stays text. SemVer stays text too: parsing it here
// would make contracts depend on compiled foundation code.
[[nodiscard]] inline Arg arg_value(const WireArg& arg) {
    switch (arg.kind) {
        case ArgKind::Signed:
            if (auto v = parse_number<i64>(arg.value)) return *v;
            break;
        case ArgKind::Unsigned:
            if (auto v = parse_number<u64>(arg.value)) return *v;
            break;
        case ArgKind::Bool:
            if (arg.value == "0" || arg.value == "1") return arg.value == "1";
            break;
        case ArgKind::Millis:
            if (auto v = parse_number<i64>(arg.value)) return std::chrono::milliseconds{*v};
            break;
        case ArgKind::Path:
            return WirePath{arg.value, {}};
        case ArgKind::String:
        case ArgKind::SemVer:
            break;
    }
    return arg.value;
}

}  // namespace detail

[[nodiscard]] inline WireDiagnostic to_wire(const Diagnostic& diag) {
    WireDiagnostic wire;
    wire.id = diag.id;
    wire.args.reserve(diag.args.size());
    for (const auto& [name, value] : diag.args)
        wire.args.push_back(WireArg{name, static_cast<ArgKind>(value.index()), detail::arg_text(value)});
    wire.detail = diag.detail;
    if (diag.os_error) {
        wire.os_origin = diag.os_error->origin;
        wire.os_code = diag.os_error->code;
    }
    wire.retryable = diag.retryable;
    wire.kind = diag.kind;
    return wire;
}

[[nodiscard]] inline Diagnostic to_diagnostic(const WireDiagnostic& wire) {
    Diagnostic diag;
    diag.domain = domain_from_id(wire.id);
    diag.id = wire.id;
    diag.kind = wire.kind;
    diag.args.reserve(wire.args.size());
    for (const auto& arg : wire.args) diag.args.emplace_back(arg.name, detail::arg_value(arg));
    diag.detail = wire.detail;
    if (wire.os_code) diag.os_error = SystemError{wire.os_origin, *wire.os_code};
    diag.retryable = wire.retryable;
    return diag;
}

}  // namespace reboot::contracts::common
