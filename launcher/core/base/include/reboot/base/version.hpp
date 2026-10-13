#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "reboot/base/result_fwd.hpp"
#include "reboot/base/types.hpp"

// Set by the build for release artifacts; IPC compatibility is an exact match on it.
#ifndef REBOOT_BUILD_ID
#define REBOOT_BUILD_ID "dev"
#endif

namespace rb {

namespace detail {

[[nodiscard]] inline bool all_digits(std::string_view text) noexcept {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

[[nodiscard]] inline std::vector<std::string_view> split_on(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end - start));
        if (end == std::string_view::npos) return parts;
        start = end + 1;
    }
}

[[nodiscard]] inline std::strong_ordering compare_semver_identifier(std::string_view a, std::string_view b) noexcept {
    const bool a_numeric = all_digits(a);
    const bool b_numeric = all_digits(b);
    if (a_numeric && b_numeric) {
        if (a.size() != b.size()) return a.size() <=> b.size();
        return a.compare(b) <=> 0;
    }
    if (a_numeric != b_numeric) return a_numeric ? std::strong_ordering::less : std::strong_ordering::greater;
    return a.compare(b) <=> 0;
}

}  // namespace detail

// parse() is defined in reboot/base/diag.hpp, which every caller includes for Result's Diagnostic.
struct SemVer {
    u32 major = 0;
    u32 minor = 0;
    u32 patch = 0;
    std::string pre;

    [[nodiscard]] static inline Result<SemVer> parse(std::string_view text);

    [[nodiscard]] std::string to_string() const {
        std::string out = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
        if (!pre.empty()) out += '-' + pre;
        return out;
    }

    bool operator==(const SemVer&) const = default;

    // SemVer 2.0 precedence: a pre-release sorts before its release.
    [[nodiscard]] std::strong_ordering operator<=>(const SemVer& other) const {
        if (const auto order = std::tie(major, minor, patch) <=> std::tie(other.major, other.minor, other.patch);
            order != 0)
            return order;
        if (pre.empty() || other.pre.empty()) return pre.empty() <=> other.pre.empty();
        const std::vector<std::string_view> mine = detail::split_on(pre, '.');
        const std::vector<std::string_view> theirs = detail::split_on(other.pre, '.');
        for (std::size_t i = 0; i < mine.size() && i < theirs.size(); ++i)
            if (const auto order = detail::compare_semver_identifier(mine[i], theirs[i]); order != 0) return order;
        return mine.size() <=> theirs.size();
    }
};

struct ProtocolVersion {
    u16 major = 0;
    u16 minor = 0;

    constexpr auto operator<=>(const ProtocolVersion&) const = default;
};

struct GameVersion {
    u16 major = 0;
    u16 minor = 0;
    std::optional<u16> patch;

    // Strict: digits only, major < 4096 and minor < 1024 (the sb bucket range), no changelist.
    [[nodiscard]] static inline Result<GameVersion> parse(std::string_view text);

    // At most 16 bytes, the length rbsb accepts for a server version.
    [[nodiscard]] std::string canonical() const {
        std::string out = std::to_string(major) + '.' + std::to_string(minor);
        if (patch) out += '.' + std::to_string(*patch);
        return out;
    }

    constexpr auto operator<=>(const GameVersion&) const = default;
};

struct Changelist {
    u32 value = 0;

    constexpr auto operator<=>(const Changelist&) const = default;
};

struct VersionStreams {
    static constexpr u16 abi_major = 1;
    static constexpr u16 abi_minor = 0;
    static constexpr std::string_view ipc_build = REBOOT_BUILD_ID;
    static constexpr u32 backend_protocol = 1;
    static constexpr u32 game_server_protocol = 1;
    static constexpr u16 payload_abi = 1;
    static constexpr u32 winhost_protocol = 1;
    static constexpr u32 manifest_schema = 1;
    static constexpr u32 catalog_schema = 1;
};

}  // namespace rb
