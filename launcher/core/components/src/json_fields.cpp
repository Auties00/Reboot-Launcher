#include "json_fields.hpp"

#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/string.hpp>

#include "messages.hpp"
#include "reboot/foundation/sha256.hpp"

namespace rb::components::json_fields {

namespace json = boost::json;

namespace {

[[nodiscard]] constexpr int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::optional<Sha256Digest> parse_sha256(std::string_view hex) {
    Sha256Digest out{};
    if (hex.size() != 2 * out.size()) return std::nullopt;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int high = hex_digit(hex[2 * i]);
        const int low = hex_digit(hex[2 * i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        out[i] = static_cast<u8>(high * 16 + low);
    }
    return out;
}

std::string member_path(std::string_view parent, std::string_view key) {
    std::string out(parent);
    if (!out.empty()) out += '.';
    out += key;
    return out;
}

std::string index_path(std::string_view parent, std::size_t index) {
    return std::string(parent) + "[" + std::to_string(index) + "]";
}

Diagnostic malformed(std::string field) {
    return make_diag(ErrorDomain::Components, kManifestMalformed).arg("field", std::move(field)).build();
}

Result<Reader> Reader::of(const json::value& value, std::string path) {
    const json::object* object = value.if_object();
    if (object == nullptr) return std::unexpected(malformed(std::move(path)));
    return Reader(*object, std::move(path));
}

const json::value* Reader::find(std::string_view key) const {
    const json::value* value = object_->if_contains(key);
    if (value == nullptr || value->is_null()) return nullptr;
    return value;
}

Result<const json::value*> Reader::required(std::string_view key) const {
    const json::value* value = find(key);
    if (value == nullptr) return std::unexpected(malformed(at(key)));
    return value;
}

Result<u64> Reader::u64_at(std::string_view key) const {
    auto value = required(key);
    if (!value) return std::unexpected(std::move(value.error()));
    if ((*value)->is_uint64()) return (*value)->get_uint64();
    if ((*value)->is_int64() && (*value)->get_int64() >= 0) return static_cast<u64>((*value)->get_int64());
    return std::unexpected(malformed(at(key)));
}

Result<std::string> Reader::string_at(std::string_view key) const {
    auto value = required(key);
    if (!value) return std::unexpected(std::move(value.error()));
    const json::string* text = (*value)->if_string();
    if (text == nullptr) return std::unexpected(malformed(at(key)));
    return std::string(text->data(), text->size());
}

Result<std::string> Reader::nonempty_string_at(std::string_view key) const {
    auto text = string_at(key);
    if (text && text->empty()) return std::unexpected(malformed(at(key)));
    return text;
}

Result<bool> Reader::optional_bool(std::string_view key) const {
    const json::value* value = find(key);
    if (value == nullptr) return false;
    if (!value->is_bool()) return std::unexpected(malformed(at(key)));
    return value->get_bool();
}

Result<SemVer> Reader::semver_at(std::string_view key) const {
    auto text = string_at(key);
    if (!text) return std::unexpected(std::move(text.error()));
    auto version = SemVer::parse(*text);
    if (!version) return std::unexpected(malformed(at(key)));
    return std::move(*version);
}

Result<std::optional<SemVer>> Reader::optional_semver(std::string_view key) const {
    if (find(key) == nullptr) return std::optional<SemVer>{};
    return semver_at(key).transform([](SemVer version) { return std::optional<SemVer>(std::move(version)); });
}

Result<std::chrono::system_clock::time_point> Reader::unix_ms_at(std::string_view key) const {
    auto unix_ms = u64_at(key);
    if (!unix_ms) return std::unexpected(std::move(unix_ms.error()));
    using std::chrono::system_clock;
    // system_clock ticks in ns on libstdc++, 100 ns on MSVC and us on libc++.
    constexpr u64 kMaxUnixMs =
        static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(system_clock::duration::max()).count());
    if (*unix_ms > kMaxUnixMs) return std::unexpected(malformed(at(key)));
    return system_clock::time_point(std::chrono::duration_cast<system_clock::duration>(
        std::chrono::milliseconds(static_cast<i64>(*unix_ms))));
}

Result<const json::array*> Reader::optional_array(std::string_view key) const {
    static const json::array kEmpty;
    const json::value* value = find(key);
    if (value == nullptr) return &kEmpty;
    const json::array* array = value->if_array();
    if (array == nullptr) return std::unexpected(malformed(at(key)));
    return array;
}

Result<RemoteFile> read_remote_file(const Reader& in) {
    RemoteFile file;
    auto urls = in.required("urls");
    if (!urls) return std::unexpected(std::move(urls.error()));
    const json::array* list = (*urls)->if_array();
    if (list == nullptr || list->empty()) return std::unexpected(malformed(in.at("urls")));
    for (std::size_t i = 0; i < list->size(); ++i) {
        const json::string* url = (*list)[i].if_string();
        if (url == nullptr || url->empty()) return std::unexpected(malformed(index_path(in.at("urls"), i)));
        file.urls.emplace_back(url->data(), url->size());
    }

    auto sha = in.string_at("sha256");
    if (!sha) return std::unexpected(std::move(sha.error()));
    auto digest = parse_sha256(*sha);
    if (!digest) return std::unexpected(malformed(in.at("sha256")));
    file.sha256 = *digest;

    auto size = in.u64_at("size");
    if (!size) return std::unexpected(std::move(size.error()));
    if (*size == 0) return std::unexpected(malformed(in.at("size")));
    file.size = *size;
    return file;
}

void write_remote_file(json::object& out, const RemoteFile& file) {
    json::array urls;
    for (const std::string& url : file.urls) urls.emplace_back(url);
    out["urls"] = std::move(urls);
    out["sha256"] = to_hex(file.sha256);
    out["size"] = file.size;
}

Result<std::optional<ManifestPlatform>> read_platform(const Reader& in) {
    auto os = in.string_at("os");
    if (!os) return std::unexpected(std::move(os.error()));
    auto arch = in.string_at("arch");
    if (!arch) return std::unexpected(std::move(arch.error()));
    const std::optional<ManifestOs> known_os = find_named(kOsNames, *os);
    const std::optional<ManifestArch> known_arch = find_named(kArchNames, *arch);
    if (!known_os || !known_arch) return std::optional<ManifestPlatform>{};
    return std::optional<ManifestPlatform>(ManifestPlatform{*known_os, *known_arch});
}

}  // namespace rb::components::json_fields
