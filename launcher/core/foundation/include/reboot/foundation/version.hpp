#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/result_fwd.hpp"
#include "reboot/foundation/types.hpp"

// Set by the build for release artifacts; IPC compatibility is an exact match on it.
#ifndef REBOOT_BUILD_ID
#define REBOOT_BUILD_ID "dev"
#endif

namespace rb {

struct SemVer {
    u32 major = 0;
    u32 minor = 0;
    u32 patch = 0;
    std::string pre;

    [[nodiscard]] static Result<SemVer> parse(std::string_view text);
    [[nodiscard]] std::string to_string() const;

    bool operator==(const SemVer&) const = default;
    // SemVer 2.0 precedence: a pre-release sorts before its release.
    [[nodiscard]] std::strong_ordering operator<=>(const SemVer& other) const;
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
    [[nodiscard]] static Result<GameVersion> parse(std::string_view text);
    // At most 16 bytes, the length rbsb accepts for a server version.
    [[nodiscard]] std::string canonical() const;
    // Byte-identical to sb version_bucket(canonical()).
    [[nodiscard]] u32 bucket() const;

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
