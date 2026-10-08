#include "reboot/foundation/native_path.hpp"

#include <concepts>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot {

namespace {

using NativeString = NativePath::string_type;

// NativePath holds UTF-16 on Windows and raw bytes on POSIX; templates keep the branch for the
// other platform uninstantiated.
template <class String>
constexpr bool kWide = std::same_as<typename String::value_type, wchar_t>;

template <class String>
std::string utf8_of(const String& native) {
    if constexpr (kWide<String>) {
        std::u16string units;
        units.reserve(native.size());
        for (const wchar_t c : native) units.push_back(static_cast<char16_t>(c));
        return utf16_to_utf8(units);
    } else {
        return utf16_to_utf8(utf8_to_utf16(native));
    }
}

template <class String>
std::vector<u8> bytes_of(const String& native) {
    if constexpr (kWide<String>) {
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
std::optional<String> native_from(const std::vector<u8>& bytes) {
    if constexpr (kWide<String>) {
        if (bytes.size() % 2 != 0) return std::nullopt;
        String out;
        out.reserve(bytes.size() / 2);
        for (std::size_t i = 0; i < bytes.size(); i += 2) out.push_back(static_cast<wchar_t>(bytes[i] | bytes[i + 1] << 8));
        return out;
    } else {
        return String(bytes.begin(), bytes.end());
    }
}

}  // namespace

WirePath to_wire(const NativePath& path) { return WirePath{utf8_of(path.native()), bytes_of(path.native())}; }

Result<NativePath> from_wire(const WirePath& path) {
    std::optional<NativeString> native = native_from<NativeString>(path.native);
    if (!native || native->find(NativePath::value_type{}) != NativeString::npos)
        return make_diag(ErrorDomain::Foundation, msg::kMalformedWirePath).kind(ErrorKind::InvalidInput).fail();
    return NativePath(std::move(*native));
}

std::string display_utf8(const NativePath& path) { return utf8_of(path.native()); }

}  // namespace reboot
