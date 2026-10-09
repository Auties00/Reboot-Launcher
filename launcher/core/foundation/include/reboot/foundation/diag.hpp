#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/result_fwd.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

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

struct ArgSpec {
    std::string_view name;
};

struct MessageSpec {
    std::string_view id;
    std::string_view english;
    std::span<const ArgSpec> args;
};

// Every message declared with REBOOT_MESSAGE in the linked program; valid after static init.
[[nodiscard]] std::span<const MessageSpec* const> message_registry();

// 0 ok, 1 generic, 2 invalid input, 3 not found, 4 conflict or busy, 5 engine unavailable,
// 6 unsupported, 130 cancelled.
[[nodiscard]] int exit_code_for(const Diagnostic& diag);

[[nodiscard]] Diagnostic internal_bug(std::string_view where);

namespace detail {

[[nodiscard]] constexpr bool is_placeholder_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Visits each distinct "{name}" in order of first appearance.
template <class F>
constexpr std::size_t for_each_placeholder(std::string_view text, F&& visit) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        std::size_t end = i + 1;
        while (end < text.size() && is_placeholder_char(text[end])) ++end;
        if (end == i + 1 || end == text.size() || text[end] != '}') continue;
        const std::string_view name = text.substr(i + 1, end - i - 1);
        if (text.substr(0, i).find(text.substr(i, end - i + 1)) == std::string_view::npos) {
            visit(count, name);
            ++count;
        }
        i = end;
    }
    return count;
}

[[nodiscard]] constexpr std::size_t count_placeholders(std::string_view text) {
    return for_each_placeholder(text, [](std::size_t, std::string_view) {});
}

template <std::size_t N>
[[nodiscard]] constexpr std::array<ArgSpec, N> placeholders(std::string_view text) {
    std::array<ArgSpec, N> out{};
    for_each_placeholder(text, [&](std::size_t index, std::string_view name) { out[index] = ArgSpec{name}; });
    return out;
}

class MessageRegistration {
public:
    explicit MessageRegistration(const MessageSpec& spec) noexcept;

    const MessageSpec* spec;
    const MessageRegistration* next;
};

}  // namespace detail

}  // namespace rb

// Used once per id, at namespace scope in the package's src/messages.cpp.
#define REBOOT_MESSAGE(ident, dotted_id, english)                                                        \
    static constexpr auto ident##_arg_specs_ =                                                           \
        ::rb::detail::placeholders<::rb::detail::count_placeholders(english)>(english);         \
    static constexpr ::rb::MessageSpec ident##_spec_{dotted_id, english, ident##_arg_specs_};      \
    static const ::rb::detail::MessageRegistration ident##_registration_{ident##_spec_};           \
    extern const ::rb::MessageId ident;                                                             \
    const ::rb::MessageId ident { dotted_id }

// Declares an id for the package's other translation units (src/messages.hpp).
#define REBOOT_MESSAGE_DECL(ident) extern const ::rb::MessageId ident
