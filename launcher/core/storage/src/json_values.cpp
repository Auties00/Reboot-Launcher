#include "reboot/storage/json_values.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <limits>
#include <string>
#include <vector>

#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>

#include "json_text.hpp"
#include "member_reader.hpp"
#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::storage {

namespace json = boost::json;

namespace {

constexpr std::string_view kNativeMember = "native";

[[nodiscard]] std::string_view view(const json::string& text) noexcept { return {text.data(), text.size()}; }

[[nodiscard]] bool well_formed_utf16(std::wstring_view text) noexcept {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto unit = static_cast<u32>(text[i]);
        if (unit >= 0xDC00 && unit <= 0xDFFF) return false;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 1 == text.size()) return false;
            const auto next = static_cast<u32>(text[i + 1]);
            if (next < 0xDC00 || next > 0xDFFF) return false;
            ++i;
        }
    }
    return true;
}

[[nodiscard]] Diagnostic out_of_range(const json::value& value) {
    return invalid_input(msg::kOutOfRange).arg("value", json::serialize(value)).build();
}

// Templates, so only the branch for this platform's path encoding is instantiated.
template <class Native>
[[nodiscard]] json::value native_to_json(const NativePath& path, const Native& native) {
    std::vector<u8> bytes;
    if constexpr (std::same_as<typename Native::value_type, wchar_t>) {
        if (well_formed_utf16(native)) {
            const std::u8string utf8 = path.u8string();
            return json::string(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
        }
        for (const wchar_t unit : native) {
            bytes.push_back(static_cast<u8>(static_cast<u32>(unit) & 0xFF));
            bytes.push_back(static_cast<u8>(static_cast<u32>(unit) >> 8));
        }
    } else {
        if (is_valid_utf8(native)) return json::string(native);
        bytes.assign(native.begin(), native.end());
    }
    json::object object;
    object.emplace(kNativeMember, base64_encode(bytes));
    return object;
}

template <class Native>
[[nodiscard]] Result<NativePath> native_from_bytes(const std::vector<u8>& bytes) {
    if constexpr (std::same_as<typename Native::value_type, wchar_t>) {
        if (bytes.size() % 2 != 0) return invalid_input(msg::kInvalidPath).fail();
        Native wide;
        for (std::size_t i = 0; i < bytes.size(); i += 2)
            wide.push_back(static_cast<wchar_t>(u32{bytes[i]} | (u32{bytes[i + 1]} << 8)));
        return NativePath(std::move(wide));
    } else {
        return NativePath(Native(bytes.begin(), bytes.end()));
    }
}

}  // namespace

json::value name_to_json(std::string_view name) { return json::string(name); }

Result<std::size_t> name_index_from_json(const json::value& value, std::span<const std::string_view> names) {
    const json::string* text = value.if_string();
    if (text == nullptr) return std::unexpected(wrong_type("string"));
    const auto found = std::ranges::find(names, view(*text));
    if (found == names.end()) return invalid_input(msg::kUnknownName).arg("value", view(*text)).fail();
    return static_cast<std::size_t>(found - names.begin());
}

json::value path_to_json(const NativePath& path) { return native_to_json(path, path.native()); }

Result<NativePath> path_from_json(const json::value& value) {
    if (const json::string* text = value.if_string()) {
        if (text->empty() || !is_valid_utf8(view(*text))) return invalid_input(msg::kInvalidPath).fail();
        return NativePath(std::u8string(text->begin(), text->end()));
    }
    const json::object* object = value.if_object();
    if (object == nullptr) return std::unexpected(wrong_type("string"));
    const json::value* encoded = object->if_contains(kNativeMember);
    if (encoded == nullptr || !encoded->is_string()) return invalid_input(msg::kInvalidPath).fail();
    const std::optional<std::vector<u8>> bytes = base64_decode(view(encoded->get_string()));
    if (!bytes || bytes->empty()) return invalid_input(msg::kInvalidPath).fail();
    return native_from_bytes<NativePath::string_type>(*bytes);
}

json::value time_to_json(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
}

Result<std::chrono::system_clock::time_point> time_from_json(const json::value& value) {
    using std::chrono::microseconds;
    using std::chrono::system_clock;
    const Result<u64> micros = u64_from_json(value);
    if (!micros) return std::unexpected(micros.error());
    const auto limit = std::chrono::duration_cast<microseconds>(system_clock::duration::max()).count();
    if (*micros > static_cast<u64>(limit)) return std::unexpected(out_of_range(value));
    return system_clock::time_point(
        std::chrono::duration_cast<system_clock::duration>(microseconds(static_cast<microseconds::rep>(*micros))));
}

json::value uuid_to_json(const Uuid& uuid) { return json::string(format_uuid(uuid)); }

Result<Uuid> uuid_from_json(const json::value& value) {
    const json::string* text = value.if_string();
    if (text == nullptr) return std::unexpected(wrong_type("string"));
    Result<Uuid> uuid = parse_uuid(view(*text));
    if (!uuid) return invalid_input(msg::kInvalidUuid).arg("value", view(*text)).fail();
    return uuid;
}

json::value semver_to_json(const SemVer& version) { return json::string(version.to_string()); }

Result<SemVer> semver_from_json(const json::value& value) {
    const json::string* text = value.if_string();
    if (text == nullptr) return std::unexpected(wrong_type("string"));
    Result<SemVer> version = SemVer::parse(view(*text));
    if (!version) return invalid_input(msg::kInvalidVersion).arg("value", view(*text)).fail();
    return version;
}

json::value game_version_to_json(const GameVersion& version) { return json::string(version.canonical()); }

Result<GameVersion> game_version_from_json(const json::value& value) {
    const json::string* text = value.if_string();
    if (text == nullptr) return std::unexpected(wrong_type("string"));
    Result<GameVersion> version = GameVersion::parse(view(*text));
    if (!version) return invalid_input(msg::kInvalidVersion).arg("value", view(*text)).fail();
    return version;
}

json::value port_to_json(Port port) { return port.value; }

Result<Port> port_from_json(const json::value& value) {
    const Result<u64> number = u64_from_json(value);
    if (!number) return std::unexpected(number.error());
    if (*number == 0 || *number > std::numeric_limits<u16>::max()) return std::unexpected(out_of_range(value));
    return Port{static_cast<u16>(*number)};
}

Result<std::string> string_from_json(const json::value& value) {
    const json::string* text = value.if_string();
    if (text == nullptr) return std::unexpected(wrong_type("string"));
    if (!is_valid_utf8(view(*text))) return invalid_input(msg::kInvalidUtf8).fail();
    return std::string(view(*text));
}

Result<bool> bool_from_json(const json::value& value) {
    if (const bool* flag = value.if_bool()) return *flag;
    return std::unexpected(wrong_type("boolean"));
}

Result<u64> u64_from_json(const json::value& value) {
    if (const std::uint64_t* number = value.if_uint64()) return *number;
    if (const std::int64_t* number = value.if_int64()) {
        if (*number < 0) return std::unexpected(out_of_range(value));
        return static_cast<u64>(*number);
    }
    if (const double* number = value.if_double()) {
        // 2^53: beyond it a double no longer holds every integer exactly.
        constexpr double kExactLimit = 9007199254740992.0;
        if (*number < 0 || *number > kExactLimit || std::trunc(*number) != *number)
            return std::unexpected(out_of_range(value));
        return static_cast<u64>(*number);
    }
    return std::unexpected(wrong_type("number"));
}

}  // namespace reboot::storage
