#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/types.hpp"
#include "wire/messages.hpp"

namespace sb::registry {

struct FieldLimits {
    std::size_t name = 64;
    std::size_t description = 256;
    std::size_t author = 32;
    std::size_t version = 16;
    std::size_t password = 128;
    u32 max_players = 1000;
};

// Strict UTF-8 (no overlongs, surrogates or code points above U+10FFFF) with no control characters.
[[nodiscard]] inline bool valid_text(std::string_view s, std::size_t max_bytes, bool allow_empty) noexcept {
    if (s.size() > max_bytes || (!allow_empty && s.empty())) return false;
    const auto* p = reinterpret_cast<const u8*>(s.data());
    const auto* end = p + s.size();
    while (p < end) {
        const u8 c = *p;
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7F) return false;
            ++p;
            continue;
        }
        u32 cp;
        int extra;
        if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            return false;
        }
        if (end - p <= extra) return false;
        for (int i = 1; i <= extra; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        static constexpr u32 min_cp[] = {0, 0x80, 0x800, 0x10000};
        if (cp < min_cp[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x80 && cp <= 0x9F) return false;  // C1 controls
        p += extra + 1;
    }
    return true;
}

// Maps a free-form game version ("1.7.2", "4.5", "34.10", "Cert") to a bucket:
// leading "major[.minor]" digits give 2 + major * 1024 + minor, anything else is kBucketOther.
[[nodiscard]] inline u32 version_bucket(std::string_view v) noexcept {
    std::size_t i = 0;
    auto digits = [&](u32& out) {
        const std::size_t start = i;
        out = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9' && i - start < 4) out = out * 10 + static_cast<u32>(v[i++] - '0');
        return i > start;
    };
    u32 major = 0, minor = 0;
    if (!digits(major) || major >= 4096) return wire::kBucketOther;
    if (i < v.size() && v[i] == '.') {
        ++i;
        if (!digits(minor) || minor >= 1024) minor = 0;
    }
    return 2 + major * 1024 + minor;
}

struct ValidationError {
    const char* field;
};

[[nodiscard]] inline std::optional<ValidationError> validate_register(const wire::HostRegister& r,
                                                                    const FieldLimits& lim) {
    if (r.id.is_nil()) return ValidationError{"id"};
    if (!valid_text(r.name, lim.name, false)) return ValidationError{"name"};
    if (!valid_text(r.description, lim.description, true)) return ValidationError{"description"};
    if (!valid_text(r.version, lim.version, false)) return ValidationError{"version"};
    if (!valid_text(r.author, lim.author, true)) return ValidationError{"author"};
    if (r.game_port == 0 || r.game_port > 65535) return ValidationError{"game_port"};
    if (r.password && r.password->size() > lim.password) return ValidationError{"password"};
    if (r.max_players > lim.max_players) return ValidationError{"max_players"};
    if (r.players > lim.max_players) return ValidationError{"players"};
    return std::nullopt;
}

[[nodiscard]] inline std::optional<ValidationError> validate_update(const wire::HostUpdate& u, const FieldLimits& lim) {
    if (u.name && !valid_text(*u.name, lim.name, false)) return ValidationError{"name"};
    if (u.description && !valid_text(*u.description, lim.description, true)) return ValidationError{"description"};
    if (u.version && !valid_text(*u.version, lim.version, false)) return ValidationError{"version"};
    if (u.author && !valid_text(*u.author, lim.author, true)) return ValidationError{"author"};
    if (u.game_port && (*u.game_port == 0 || *u.game_port > 65535)) return ValidationError{"game_port"};
    if (u.password && u.password->size() > lim.password) return ValidationError{"password"};
    if (u.max_players && *u.max_players > lim.max_players) return ValidationError{"max_players"};
    if (u.players && *u.players > lim.max_players) return ValidationError{"players"};
    return std::nullopt;
}

// ASCII case folding for sorting and search; non-ASCII bytes compare as-is.
[[nodiscard]] inline std::string fold(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace sb::registry
