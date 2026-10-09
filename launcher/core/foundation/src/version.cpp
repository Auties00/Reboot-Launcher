#include "reboot/foundation/version.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <system_error>
#include <tuple>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "registry/validation.hpp"

namespace rb {

namespace {

bool all_digits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

template <class T>
std::optional<T> parse_number(std::string_view text, u64 max) {
    if (!all_digits(text)) return std::nullopt;
    u64 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value > max) return std::nullopt;
    return static_cast<T>(value);
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end - start));
        if (end == std::string_view::npos) return parts;
        start = end + 1;
    }
}

bool has_leading_zero(std::string_view digits) { return digits.size() > 1 && digits.front() == '0'; }

bool valid_identifier(std::string_view identifier) {
    return !identifier.empty() && std::ranges::all_of(identifier, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
    });
}

std::strong_ordering compare_identifier(std::string_view a, std::string_view b) {
    const bool a_numeric = all_digits(a);
    const bool b_numeric = all_digits(b);
    if (a_numeric && b_numeric) {
        if (a.size() != b.size()) return a.size() <=> b.size();
        return a.compare(b) <=> 0;
    }
    if (a_numeric != b_numeric) return a_numeric ? std::strong_ordering::less : std::strong_ordering::greater;
    return a.compare(b) <=> 0;
}

}  // namespace

Result<SemVer> SemVer::parse(std::string_view text) {
    const auto invalid = [&] {
        return make_diag(ErrorDomain::Foundation, msg::kInvalidSemVer).kind(ErrorKind::InvalidInput).arg("text", text).fail();
    };
    // Build metadata does not take part in precedence, so it is accepted and dropped.
    const std::string_view without_build = text.substr(0, text.find('+'));
    if (without_build.size() != text.size() && !std::ranges::all_of(split(text.substr(without_build.size() + 1), '.'),
                                                                    valid_identifier))
        return invalid();

    const std::size_t dash = without_build.find('-');
    const std::string_view core = without_build.substr(0, dash);
    const std::vector<std::string_view> numbers = split(core, '.');
    if (numbers.size() != 3) return invalid();

    SemVer version;
    u32* const fields[] = {&version.major, &version.minor, &version.patch};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto value = parse_number<u32>(numbers[i], std::numeric_limits<u32>::max());
        if (!value || has_leading_zero(numbers[i])) return invalid();
        *fields[i] = *value;
    }

    if (dash != std::string_view::npos) {
        const std::string_view pre = without_build.substr(dash + 1);
        for (const std::string_view identifier : split(pre, '.'))
            if (!valid_identifier(identifier) || (all_digits(identifier) && has_leading_zero(identifier)))
                return invalid();
        version.pre = std::string(pre);
    }
    return version;
}

std::string SemVer::to_string() const {
    std::string out = std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
    if (!pre.empty()) out += '-' + pre;
    return out;
}

std::strong_ordering SemVer::operator<=>(const SemVer& other) const {
    if (const auto order = std::tie(major, minor, patch) <=> std::tie(other.major, other.minor, other.patch); order != 0)
        return order;
    if (pre.empty() || other.pre.empty()) return pre.empty() <=> other.pre.empty();

    const std::vector<std::string_view> mine = split(pre, '.');
    const std::vector<std::string_view> theirs = split(other.pre, '.');
    for (std::size_t i = 0; i < mine.size() && i < theirs.size(); ++i)
        if (const auto order = compare_identifier(mine[i], theirs[i]); order != 0) return order;
    return mine.size() <=> theirs.size();
}

Result<GameVersion> GameVersion::parse(std::string_view text) {
    const auto invalid = [&] {
        return make_diag(ErrorDomain::Foundation, msg::kInvalidGameVersion)
            .kind(ErrorKind::InvalidInput)
            .arg("text", text)
            .fail();
    };
    const std::vector<std::string_view> parts = split(text, '.');
    if (parts.size() < 2 || parts.size() > 3) return invalid();
    const auto major = parse_number<u16>(parts[0], 4095);
    const auto minor = parse_number<u16>(parts[1], 1023);
    if (!major || !minor) return invalid();

    GameVersion version{*major, *minor, std::nullopt};
    if (parts.size() == 3) {
        version.patch = parse_number<u16>(parts[2], std::numeric_limits<u16>::max());
        if (!version.patch) return invalid();
    }
    return version;
}

std::string GameVersion::canonical() const {
    std::string out = std::to_string(major) + '.' + std::to_string(minor);
    if (patch) out += '.' + std::to_string(*patch);
    return out;
}

u32 GameVersion::bucket() const { return sb::registry::version_bucket(canonical()); }

}  // namespace rb
