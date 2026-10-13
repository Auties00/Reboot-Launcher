#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/base/native_path.hpp"
#include "reboot/base/result_fwd.hpp"
#include "reboot/base/types.hpp"
#include "reboot/base/version.hpp"

namespace rb {

enum class Severity : u8 { Info, Warning, Error };

// Unknown is for ids received over a wire whose prefix this build does not know.
enum class ErrorDomain : u8 {
    Unknown,
    Internal,
    Foundation,
    Contracts,
    Requests,
    Api,
    Posix,
    Logging,
    Trust,
    Storage,
    Ux,
    Net,
    Components,
    Catalog,
    Support,
    Builds,
    Process,
    Secrets,
    Identity,
    Injection,
    GameChannel,
    Compat,
    Backend,
    Front,
    GameServer,
    Sessions,
    Browser,
    Publish,
    Host,
    Play,
    Updates,
    Integration,
    Ipc,
    Engine,
    Client,
    Platform,
};

// The id prefix ("play" in "play.wrong_session") of each domain.
[[nodiscard]] constexpr std::string_view domain_prefix(ErrorDomain domain) noexcept {
    constexpr std::array<std::string_view, 36> kPrefixes{
        "",        "internal", "foundation", "contracts",   "requests", "api",          "posix",   "logging",
        "trust",   "storage",  "ux",         "net",         "components", "catalog",    "support", "builds",
        "process", "secrets",  "identity",   "injection",   "game_channel", "compat",   "backend", "front",
        "gameserver", "sessions", "browser", "publish",     "host",     "play",         "updates", "integration",
        "ipc",     "engine",   "client",     "platform"};
    return kPrefixes[static_cast<std::size_t>(domain)];
}

[[nodiscard]] constexpr ErrorDomain domain_from_id(std::string_view id) noexcept {
    const std::string_view prefix = id.substr(0, id.find('.'));
    for (u8 i = 1; i <= static_cast<u8>(ErrorDomain::Platform); ++i)
        if (domain_prefix(static_cast<ErrorDomain>(i)) == prefix) return static_cast<ErrorDomain>(i);
    return ErrorDomain::Unknown;
}

// Drives exit_code_for; set by the code that knows why it failed.
enum class ErrorKind : u8 { Generic, InvalidInput, NotFound, Conflict, EngineUnavailable, Unsupported, Cancelled };

struct MessageId {
    std::string_view id;

    constexpr bool operator==(const MessageId&) const = default;
};

using Arg = std::variant<std::string, i64, u64, bool, std::chrono::milliseconds, WirePath, SemVer>;

struct SystemError {
    enum class Origin : u8 { Host, GuestWindows };

    Origin origin = Origin::Host;
    i64 code = 0;

    bool operator==(const SystemError&) const = default;
};

struct LogRef {
    std::string file;
    u64 seq = 0;
};

// Ids and arg names are owned so a Diagnostic decoded from a wire stays valid.
struct Diagnostic {
    ErrorDomain domain = ErrorDomain::Unknown;
    std::string id;
    Severity severity = Severity::Error;
    ErrorKind kind = ErrorKind::Generic;
    std::vector<std::pair<std::string, Arg>> args;
    std::optional<std::string> detail;
    std::optional<SystemError> os_error;
    bool retryable = false;
    std::optional<LogRef> log_ref;
    std::vector<Diagnostic> causes;

    [[nodiscard]] bool is(MessageId message) const noexcept { return id == message.id; }

    [[nodiscard]] const Arg* find_arg(std::string_view name) const noexcept {
        for (const auto& [arg_name, value] : args)
            if (arg_name == name) return &value;
        return nullptr;
    }
};

namespace detail {

template <class T>
struct is_duration : std::false_type {};
template <class Rep, class Period>
struct is_duration<std::chrono::duration<Rep, Period>> : std::true_type {};

template <class V>
[[nodiscard]] Arg to_arg(V&& value) {
    using T = std::remove_cvref_t<V>;
    if constexpr (std::same_as<T, Arg> || std::same_as<T, WirePath> || std::same_as<T, SemVer>) {
        return Arg{std::forward<V>(value)};
    } else if constexpr (std::same_as<T, bool>) {
        return Arg{value};
    } else if constexpr (std::signed_integral<T>) {
        return Arg{static_cast<i64>(value)};
    } else if constexpr (std::unsigned_integral<T>) {
        return Arg{static_cast<u64>(value)};
    } else if constexpr (std::is_enum_v<T>) {
        return Arg{static_cast<u64>(static_cast<std::underlying_type_t<T>>(value))};
    } else if constexpr (is_duration<T>::value) {
        return Arg{std::chrono::duration_cast<std::chrono::milliseconds>(value)};
    } else if constexpr (std::same_as<T, NativePath>) {
        return Arg{to_wire(value)};
    } else {
        static_assert(std::is_convertible_v<V, std::string_view>, "unsupported diagnostic argument type");
        return Arg{std::string(std::string_view(value))};
    }
}

}  // namespace detail

class DiagBuilder {
public:
    DiagBuilder(ErrorDomain domain, MessageId message) {
        diag_.domain = domain;
        diag_.id = message.id;
    }

    template <class V>
    DiagBuilder&& arg(std::string_view name, V&& value) && {
        diag_.args.emplace_back(std::string(name), detail::to_arg(std::forward<V>(value)));
        return std::move(*this);
    }
    DiagBuilder&& detail(std::string text) && {
        diag_.detail = std::move(text);
        return std::move(*this);
    }
    DiagBuilder&& os(SystemError error) && {
        diag_.os_error = error;
        return std::move(*this);
    }
    DiagBuilder&& retryable(bool value = true) && {
        diag_.retryable = value;
        return std::move(*this);
    }
    DiagBuilder&& cause(Diagnostic cause) && {
        diag_.causes.push_back(std::move(cause));
        return std::move(*this);
    }
    DiagBuilder&& severity(Severity value) && {
        diag_.severity = value;
        return std::move(*this);
    }
    DiagBuilder&& kind(ErrorKind value) && {
        diag_.kind = value;
        return std::move(*this);
    }
    DiagBuilder&& log_ref(LogRef ref) && {
        diag_.log_ref = std::move(ref);
        return std::move(*this);
    }

    [[nodiscard]] Diagnostic build() && { return std::move(diag_); }
    operator Diagnostic() && { return std::move(diag_); }
    [[nodiscard]] std::unexpected<Diagnostic> fail() && { return std::unexpected(std::move(diag_)); }

private:
    Diagnostic diag_;
};

[[nodiscard]] inline DiagBuilder make_diag(ErrorDomain domain, MessageId message) { return {domain, message}; }

// Ids the parsers below report; reboot_foundation registers their English text.
inline constexpr MessageId kInvalidUuid{"foundation.invalid_uuid"};
inline constexpr MessageId kInvalidSemVer{"foundation.invalid_semver"};
inline constexpr MessageId kInvalidGameVersion{"foundation.invalid_game_version"};
inline constexpr MessageId kMalformedWirePath{"foundation.malformed_wire_path"};

namespace detail {

template <class T>
[[nodiscard]] std::optional<T> parse_version_number(std::string_view text, u64 max) {
    if (!all_digits(text)) return std::nullopt;
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value > max) return std::nullopt;
    return static_cast<T>(value);
}

[[nodiscard]] inline bool has_leading_zero(std::string_view digits) noexcept {
    return digits.size() > 1 && digits.front() == '0';
}

[[nodiscard]] inline bool valid_semver_identifier(std::string_view identifier) noexcept {
    return !identifier.empty() && std::ranges::all_of(identifier, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
    });
}

}  // namespace detail

// The parsers that value headers declare: each returns Result, so every caller includes this header.
inline Result<Uuid> parse_uuid(std::string_view text) {
    const auto invalid = [&] {
        return make_diag(ErrorDomain::Foundation, kInvalidUuid).kind(ErrorKind::InvalidInput).arg("text", text).fail();
    };
    if (text.size() != 36) return invalid();
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool dash_position = i == 8 || i == 13 || i == 18 || i == 23;
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (dash_position ? c != '-' : !hex) return invalid();
    }
    const std::optional<Uuid> uuid = Uuid::parse(text);
    if (!uuid) return invalid();
    return *uuid;
}

inline Result<SemVer> SemVer::parse(std::string_view text) {
    const auto invalid = [&] {
        return make_diag(ErrorDomain::Foundation, kInvalidSemVer).kind(ErrorKind::InvalidInput).arg("text", text).fail();
    };
    // Build metadata does not take part in precedence, so it is accepted and dropped.
    const std::string_view without_build = text.substr(0, text.find('+'));
    if (without_build.size() != text.size() &&
        !std::ranges::all_of(detail::split_on(text.substr(without_build.size() + 1), '.'), detail::valid_semver_identifier))
        return invalid();

    const std::size_t dash = without_build.find('-');
    const std::string_view core = without_build.substr(0, dash);
    const std::vector<std::string_view> numbers = detail::split_on(core, '.');
    if (numbers.size() != 3) return invalid();

    SemVer version;
    u32* const fields[] = {&version.major, &version.minor, &version.patch};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto value = detail::parse_version_number<u32>(numbers[i], (std::numeric_limits<u32>::max)());
        if (!value || detail::has_leading_zero(numbers[i])) return invalid();
        *fields[i] = *value;
    }

    if (dash != std::string_view::npos) {
        const std::string_view pre = without_build.substr(dash + 1);
        for (const std::string_view identifier : detail::split_on(pre, '.'))
            if (!detail::valid_semver_identifier(identifier) ||
                (detail::all_digits(identifier) && detail::has_leading_zero(identifier)))
                return invalid();
        version.pre = std::string(pre);
    }
    return version;
}

inline Result<GameVersion> GameVersion::parse(std::string_view text) {
    const auto invalid = [&] {
        return make_diag(ErrorDomain::Foundation, kInvalidGameVersion).kind(ErrorKind::InvalidInput).arg("text", text).fail();
    };
    const std::vector<std::string_view> parts = detail::split_on(text, '.');
    if (parts.size() < 2 || parts.size() > 3) return invalid();
    const auto major = detail::parse_version_number<u16>(parts[0], 4095);
    const auto minor = detail::parse_version_number<u16>(parts[1], 1023);
    if (!major || !minor) return invalid();

    GameVersion version{*major, *minor, std::nullopt};
    if (parts.size() == 3) {
        version.patch = detail::parse_version_number<u16>(parts[2], (std::numeric_limits<u16>::max)());
        if (!version.patch) return invalid();
    }
    return version;
}

inline Result<NativePath> from_wire(const WirePath& path) {
    using NativeString = NativePath::string_type;
    std::optional<NativeString> native = detail::native_from_bytes<NativeString>(path.native);
    if (!native || native->find(NativePath::value_type{}) != NativeString::npos)
        return make_diag(ErrorDomain::Foundation, kMalformedWirePath).kind(ErrorKind::InvalidInput).fail();
    return NativePath(std::move(*native));
}

}  // namespace rb
