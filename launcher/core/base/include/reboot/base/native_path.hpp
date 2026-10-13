#pragma once

#include <concepts>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "reboot/base/result_fwd.hpp"
#include "reboot/base/types.hpp"
#include "reboot/base/utf.hpp"

namespace rb {

using NativePath = std::filesystem::path;

// A path that crosses a process boundary without loss: `native` is UTF-16LE on Windows and
// the raw bytes on POSIX; `display` is for messages only.
struct WirePath {
    std::string display;
    std::vector<u8> native;

    bool operator==(const WirePath&) const = default;
};

namespace detail {

// NativePath holds UTF-16 on Windows and raw bytes on POSIX; templates keep the branch for the
// other platform uninstantiated.
template <class String>
inline constexpr bool kWideNative = std::same_as<typename String::value_type, wchar_t>;

template <class String>
[[nodiscard]] std::string native_utf8(const String& native) {
    if constexpr (kWideNative<String>) {
        std::u16string units;
        units.reserve(native.size());
        for (const wchar_t c : native) units.push_back(static_cast<char16_t>(c));
        return utf16_to_utf8(units);
    } else {
        return utf16_to_utf8(utf8_to_utf16(native));
    }
}

template <class String>
[[nodiscard]] std::vector<u8> native_bytes(const String& native) {
    if constexpr (kWideNative<String>) {
        std::vector<u8> out;
        out.reserve(native.size() * 2);
        for (const wchar_t c : native) {
            const auto unit = static_cast<char16_t>(c);
            out.push_back(static_cast<u8>(unit));
            out.push_back(static_cast<u8>(unit >> 8));
        }
        return out;
    } else {
        return {native.begin(), native.end()};
    }
}

template <class String>
[[nodiscard]] std::optional<String> native_from_bytes(const std::vector<u8>& bytes) {
    if constexpr (kWideNative<String>) {
        if (bytes.size() % 2 != 0) return std::nullopt;
        String out;
        out.reserve(bytes.size() / 2);
        for (std::size_t i = 0; i < bytes.size(); i += 2) out.push_back(static_cast<wchar_t>(bytes[i] | bytes[i + 1] << 8));
        return out;
    } else {
        return String(bytes.begin(), bytes.end());
    }
}

}  // namespace detail

[[nodiscard]] inline WirePath to_wire(const NativePath& path) {
    return WirePath{detail::native_utf8(path.native()), detail::native_bytes(path.native())};
}

// Defined in reboot/base/diag.hpp, which every caller includes for Result's Diagnostic.
[[nodiscard]] inline Result<NativePath> from_wire(const WirePath& path);

// Lossy; never feed the result back into a path.
[[nodiscard]] inline std::string display_utf8(const NativePath& path) { return detail::native_utf8(path.native()); }

}  // namespace rb
