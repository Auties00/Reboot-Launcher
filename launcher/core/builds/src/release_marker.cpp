#include "reboot/builds/release_marker.hpp"

#include <charconv>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <utility>

#include "release_marker_scan.hpp"

namespace reboot::builds {

namespace {

constexpr std::string_view kMarker = kReleaseMarkerText;
// u32 has at most 10 decimal digits.
constexpr std::size_t kMaxClDigits = 10;

// UTF-16LE code units indexed from `start`; negative indices reach back before it.
class Utf16Units {
public:
    Utf16Units(std::span<const u8> bytes, std::size_t start) noexcept
        : bytes_(bytes), start_(static_cast<std::ptrdiff_t>(start)) {}

    [[nodiscard]] bool has(std::ptrdiff_t index) const noexcept {
        const std::ptrdiff_t offset = start_ + (index * 2);
        return offset >= 0 && static_cast<std::size_t>(offset) + 1 < bytes_.size();
    }
    // Only for an index has() accepted.
    [[nodiscard]] u16 at(std::ptrdiff_t index) const noexcept {
        const auto offset = static_cast<std::size_t>(start_ + (index * 2));
        return static_cast<u16>(bytes_[offset] | (bytes_[offset + 1] << 8));
    }

private:
    std::span<const u8> bytes_;
    std::ptrdiff_t start_;
};

[[nodiscard]] constexpr bool is_digit(u16 unit) noexcept { return unit >= '0' && unit <= '9'; }
[[nodiscard]] constexpr bool is_printable_ascii(u16 unit) noexcept { return unit > 0x20 && unit < 0x7F; }

[[nodiscard]] std::optional<u32> parse_u32(std::string_view digits) {
    u32 value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    return value;
}

// "X.Y.Z-<cl>+" right before the marker; anything else means the string carries no engine changelist.
[[nodiscard]] std::optional<Changelist> engine_changelist(const Utf16Units& units) {
    std::ptrdiff_t i = -1;
    if (!units.has(i) || units.at(i) != '+') return std::nullopt;
    std::string digits;
    for (--i; units.has(i) && is_digit(units.at(i)) && digits.size() <= kMaxClDigits; --i)
        digits.insert(digits.begin(), static_cast<char>(units.at(i)));
    if (digits.empty() || digits.size() > kMaxClDigits || !units.has(i) || units.at(i) != '-') return std::nullopt;

    int dots = 0;
    bool component = false;
    for (--i; units.has(i) && (is_digit(units.at(i)) || units.at(i) == '.'); --i) {
        if (units.at(i) == '.') {
            if (!component) return std::nullopt;
            ++dots;
            component = false;
        } else {
            component = true;
        }
    }
    if (dots != 2 || !component) return std::nullopt;

    const std::optional<u32> value = parse_u32(digits);
    if (!value) return std::nullopt;
    return Changelist{*value};
}

[[nodiscard]] std::optional<ReleaseMarker> match_at(std::span<const u8> utf16le, std::size_t offset) {
    const Utf16Units units(utf16le, offset);
    for (std::size_t i = 0; i < kMarker.size(); ++i) {
        const auto index = static_cast<std::ptrdiff_t>(i);
        if (!units.has(index) || units.at(index) != static_cast<u8>(kMarker[i])) return std::nullopt;
    }

    std::string tail;
    for (auto i = static_cast<std::ptrdiff_t>(kMarker.size()); units.has(i) && units.at(i) != 0; ++i) {
        if (!is_printable_ascii(units.at(i)) || tail.size() == kReleaseTailCap) return std::nullopt;
        tail.push_back(static_cast<char>(units.at(i)));
    }
    if (tail.empty()) return std::nullopt;

    return ReleaseMarker{.engine_cl = engine_changelist(units), .tail = std::move(tail)};
}

}  // namespace

std::optional<MarkerMatch> find_release_marker_from(std::span<const u8> utf16le, std::size_t first) {
    const std::size_t marker_bytes = kMarker.size() * 2;
    for (std::size_t offset = first; offset + marker_bytes <= utf16le.size(); ++offset)
        if (auto marker = match_at(utf16le, offset)) return MarkerMatch{.marker = std::move(*marker), .offset = offset};
    return std::nullopt;
}

std::optional<ReleaseMarker> find_release_marker(std::span<const u8> utf16le) {
    if (auto match = find_release_marker_from(utf16le, 0)) return std::move(match->marker);
    return std::nullopt;
}

}  // namespace reboot::builds
