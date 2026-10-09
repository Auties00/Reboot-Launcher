#include "reboot/catalog/parse_catalog.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include "reboot/foundation/diag.hpp"

namespace reboot::catalog {

namespace {

namespace json = boost::json;

using Unexpected = std::unexpected<CatalogError>;

template <class T>
using Parsed = std::expected<T, CatalogError>;

template <class E>
struct EnumName {
    std::string_view name;
    E value;
};

constexpr std::array<EnumName<ArchiveFormat>, 3> kFormats{{
    {"zip", ArchiveFormat::Zip},
    {"rar", ArchiveFormat::Rar},
    {"7z", ArchiveFormat::SevenZip},
}};
constexpr std::array<EnumName<ArchiveContainer>, 2> kContainers{{
    {"none", ArchiveContainer::None},
    {"zip_stored", ArchiveContainer::ZipStored},
}};
constexpr std::array<EnumName<Availability>, 3> kAvailability{{
    {"available", Availability::Available},
    {"unavailable", Availability::Unavailable},
    {"withdrawn", Availability::Withdrawn},
}};
constexpr std::array<EnumName<BootStrategy>, 2> kBootStrategies{{
    {"early_bird_apc", BootStrategy::EarlyBirdApc},
    {"after_resume", BootStrategy::AfterResume},
}};
constexpr std::array<EnumName<HotfixDelivery>, 2> kHotfixDelivery{{
    {"serve", HotfixDelivery::Serve},
    {"withhold", HotfixDelivery::Withhold},
}};
constexpr std::array<EnumName<XmppSupport>, 2> kXmppSupport{{
    {"supported", XmppSupport::Supported},
    {"unsupported", XmppSupport::Unsupported},
}};
constexpr std::array<EnumName<XmppTransport>, 2> kXmppTransports{{
    {"websocket", XmppTransport::WebSocket},
    {"raw_tcp", XmppTransport::RawTcp},
}};

[[nodiscard]] Unexpected malformed(std::string where) {
    return Unexpected(CatalogError{.code = CatalogErrorCode::Malformed, .where = std::move(where)});
}

[[nodiscard]] std::string member_path(std::string_view parent, std::string_view key) {
    std::string out(parent);
    if (!out.empty()) out += '.';
    out += key;
    return out;
}

[[nodiscard]] std::string index_path(std::string_view parent, std::size_t index) {
    return std::string(parent) + "[" + std::to_string(index) + "]";
}

// Reads members of one JSON object; every error names the member's path.
class Reader {
public:
    Reader(const json::object& object, std::string path) : object_(object), path_(std::move(path)) {}

    [[nodiscard]] static Parsed<Reader> of(const json::value& value, std::string path) {
        const json::object* object = value.if_object();
        if (object == nullptr) return malformed(std::move(path));
        return Reader(*object, std::move(path));
    }

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::string at(std::string_view key) const { return member_path(path_, key); }
    [[nodiscard]] const json::value* find(std::string_view key) const { return object_.if_contains(key); }

    [[nodiscard]] Parsed<const json::value*> required(std::string_view key) const {
        const json::value* value = find(key);
        if (value == nullptr) return malformed(at(key));
        return value;
    }

    [[nodiscard]] Parsed<u64> u64_at(std::string_view key) const {
        return required(key).and_then([&](const json::value* value) { return as_u64(*value, at(key)); });
    }

    [[nodiscard]] Parsed<std::optional<u64>> optional_u64(std::string_view key) const {
        const json::value* value = find(key);
        if (value == nullptr || value->is_null()) return std::optional<u64>{};
        return as_u64(*value, at(key)).transform([](u64 number) { return std::optional<u64>(number); });
    }

    [[nodiscard]] Parsed<std::string> string_at(std::string_view key) const {
        return required(key).and_then([&](const json::value* value) { return as_string(*value, at(key)); });
    }

    [[nodiscard]] Parsed<std::string> optional_string(std::string_view key) const {
        const json::value* value = find(key);
        if (value == nullptr || value->is_null()) return std::string();
        return as_string(*value, at(key));
    }

    [[nodiscard]] Parsed<bool> optional_bool(std::string_view key) const {
        const json::value* value = find(key);
        if (value == nullptr || value->is_null()) return false;
        if (!value->is_bool()) return malformed(at(key));
        return value->get_bool();
    }

    [[nodiscard]] Parsed<GameVersion> version_at(std::string_view key) const {
        return string_at(key).and_then([&](const std::string& text) -> Parsed<GameVersion> {
            auto version = GameVersion::parse(text);
            if (!version) return malformed(at(key));
            return *version;
        });
    }

    [[nodiscard]] Parsed<std::chrono::system_clock::time_point> time_at(std::string_view key) const {
        return u64_at(key).and_then([&](u64 unix_ms) -> Parsed<std::chrono::system_clock::time_point> {
            using std::chrono::milliseconds;
            using std::chrono::system_clock;
            // The largest time this library's system_clock tick can hold (year 2262 with nanoseconds).
            constexpr u64 kMaxUnixMs =
                static_cast<u64>(std::chrono::duration_cast<milliseconds>(system_clock::duration::max()).count());
            if (unix_ms > kMaxUnixMs) return malformed(at(key));
            return system_clock::time_point(
                std::chrono::duration_cast<system_clock::duration>(milliseconds(static_cast<i64>(unix_ms))));
        });
    }

    // Absent or unknown text gives `fallback`; a value that is not a string is Malformed.
    template <class E, std::size_t N>
    [[nodiscard]] Parsed<E> enum_at(std::string_view key, const std::array<EnumName<E>, N>& names, E fallback) const {
        const json::value* value = find(key);
        if (value == nullptr || value->is_null()) return fallback;
        const json::string* text = value->if_string();
        if (text == nullptr) return malformed(at(key));
        for (const auto& [name, enumerator] : names)
            if (name == std::string_view(*text)) return enumerator;
        return fallback;
    }

    template <class E, std::size_t N>
    [[nodiscard]] Parsed<std::optional<E>> optional_enum(std::string_view key,
                                                         const std::array<EnumName<E>, N>& names) const {
        const json::value* value = find(key);
        if (value == nullptr || value->is_null()) return std::optional<E>{};
        const json::string* text = value->if_string();
        if (text == nullptr) return malformed(at(key));
        for (const auto& [name, enumerator] : names)
            if (name == std::string_view(*text)) return std::optional<E>(enumerator);
        return std::optional<E>{};
    }

    [[nodiscard]] static Parsed<u64> as_u64(const json::value& value, std::string path) {
        if (value.is_uint64()) return value.get_uint64();
        if (value.is_int64() && value.get_int64() >= 0) return static_cast<u64>(value.get_int64());
        return malformed(std::move(path));
    }

    [[nodiscard]] static Parsed<std::string> as_string(const json::value& value, std::string path) {
        const json::string* text = value.if_string();
        if (text == nullptr) return malformed(std::move(path));
        return std::string(text->data(), text->size());
    }

private:
    const json::object& object_;
    std::string path_;
};

[[nodiscard]] constexpr int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

[[nodiscard]] std::optional<std::array<u8, 32>> decode_sha256(std::string_view hex) {
    std::array<u8, 32> out{};
    if (hex.size() != 2 * out.size()) return std::nullopt;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int high = hex_digit(hex[2 * i]);
        const int low = hex_digit(hex[2 * i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        out[i] = static_cast<u8>(high * 16 + low);
    }
    return out;
}

[[nodiscard]] std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

[[nodiscard]] Parsed<CatalogEntry> parse_entry(const Reader& in) {
    CatalogEntry entry;
    auto id = in.string_at("id");
    if (!id) return Unexpected(std::move(id.error()));
    if (id->empty()) return malformed(in.at("id"));
    entry.id = std::move(*id);

    auto version = in.version_at("version");
    if (!version) return Unexpected(std::move(version.error()));
    entry.version = *version;

    auto changelist = in.optional_u64("changelist");
    if (!changelist) return Unexpected(std::move(changelist.error()));
    if (*changelist) {
        if (**changelist > std::numeric_limits<u32>::max()) return malformed(in.at("changelist"));
        entry.changelist = Changelist{static_cast<u32>(**changelist)};
    }

    auto display_name = in.optional_string("display_name");
    if (!display_name) return Unexpected(std::move(display_name.error()));
    entry.display_name = std::move(*display_name);

    if (const json::value* aliases = in.find("aliases"); aliases != nullptr && !aliases->is_null()) {
        const json::array* list = aliases->if_array();
        if (list == nullptr) return malformed(in.at("aliases"));
        for (std::size_t i = 0; i < list->size(); ++i) {
            auto alias = Reader::as_string((*list)[i], index_path(in.at("aliases"), i));
            if (!alias) return Unexpected(std::move(alias.error()));
            if (alias->empty()) return malformed(index_path(in.at("aliases"), i));
            entry.aliases.push_back(std::move(*alias));
        }
    }

    auto url = in.string_at("url");
    if (!url) return Unexpected(std::move(url.error()));
    entry.url = std::move(*url);

    auto format = in.enum_at("format", kFormats, ArchiveFormat::Unrecognized);
    if (!format) return Unexpected(std::move(format.error()));
    entry.format = *format;

    auto container = in.enum_at("container", kContainers, ArchiveContainer::Unrecognized);
    if (!container) return Unexpected(std::move(container.error()));
    entry.container = *container;

    auto archive_size = in.u64_at("archive_size");
    if (!archive_size) return Unexpected(std::move(archive_size.error()));
    entry.archive_size = *archive_size;

    auto installed_size = in.optional_u64("installed_size");
    if (!installed_size) return Unexpected(std::move(installed_size.error()));
    entry.installed_size = *installed_size;

    auto sha256 = in.optional_string("sha256");
    if (!sha256) return Unexpected(std::move(sha256.error()));
    if (!sha256->empty()) {
        entry.sha256 = decode_sha256(*sha256);
        if (!entry.sha256) return malformed(in.at("sha256"));
    }

    auto availability = in.enum_at("availability", kAvailability, Availability::Unavailable);
    if (!availability) return Unexpected(std::move(availability.error()));
    entry.availability = *availability;
    return entry;
}

[[nodiscard]] Parsed<BootInject> parse_boot_inject(const Reader& flags) {
    BootInject out;
    const json::value* value = flags.find("boot_inject");
    if (value == nullptr || value->is_null()) return out;
    auto in = Reader::of(*value, flags.at("boot_inject"));
    if (!in) return Unexpected(std::move(in.error()));

    const std::pair<std::string_view, std::optional<BootStrategy>*> runners[] = {
        {"native", &out.native}, {"wine", &out.wine}, {"umu", &out.umu}, {"mac_runtime", &out.mac_runtime}};
    for (const auto& [key, slot] : runners) {
        auto strategy = in->optional_enum(key, kBootStrategies);
        if (!strategy) return Unexpected(std::move(strategy.error()));
        *slot = *strategy;
    }
    return out;
}

[[nodiscard]] Parsed<BuildFlags> parse_flags(const Reader& in) {
    BuildFlags flags;
    auto boot_inject = parse_boot_inject(in);
    if (!boot_inject) return Unexpected(std::move(boot_inject.error()));
    flags.boot_inject = *boot_inject;

    auto exchangecode = in.optional_bool("auth_exchangecode");
    if (!exchangecode) return Unexpected(std::move(exchangecode.error()));
    flags.auth_exchangecode = *exchangecode;

    if (const json::value* xmpp = in.find("xmpp"); xmpp != nullptr && !xmpp->is_null()) {
        auto note = Reader::of(*xmpp, in.at("xmpp"));
        if (!note) return Unexpected(std::move(note.error()));
        auto support = note->enum_at("support", kXmppSupport, XmppSupport::Unknown);
        if (!support) return Unexpected(std::move(support.error()));
        auto transport = note->enum_at("transport", kXmppTransports, XmppTransport::Unknown);
        if (!transport) return Unexpected(std::move(transport.error()));
        flags.xmpp = XmppNote{.support = *support, .transport = *transport};
    }

    auto hotfix = in.enum_at("hotfix_delivery", kHotfixDelivery, HotfixDelivery::Withhold);
    if (!hotfix) return Unexpected(std::move(hotfix.error()));
    flags.hotfix_delivery = *hotfix;

    auto ia32cap = in.optional_bool("openssl_ia32cap");
    if (!ia32cap) return Unexpected(std::move(ia32cap.error()));
    flags.openssl_ia32cap = *ia32cap;
    return flags;
}

[[nodiscard]] Parsed<BuildFlagRange> parse_range(const Reader& in) {
    BuildFlagRange range;
    auto first = in.version_at("first");
    if (!first) return Unexpected(std::move(first.error()));
    auto last = in.version_at("last");
    if (!last) return Unexpected(std::move(last.error()));
    if (*last < *first) return malformed(in.at("last"));
    range.versions = VersionRange{.first = *first, .last = *last};

    if (const json::value* flags = in.find("flags"); flags != nullptr && !flags->is_null()) {
        auto reader = Reader::of(*flags, in.at("flags"));
        if (!reader) return Unexpected(std::move(reader.error()));
        auto parsed = parse_flags(*reader);
        if (!parsed) return Unexpected(std::move(parsed.error()));
        range.flags = *parsed;
    }
    return range;
}

template <class T, class Parse>
[[nodiscard]] Parsed<std::vector<T>> parse_list(const Reader& in, std::string_view key, Parse parse) {
    auto value = in.required(key);
    if (!value) return Unexpected(std::move(value.error()));
    const json::array* list = (*value)->if_array();
    if (list == nullptr) return malformed(in.at(key));
    std::vector<T> out;
    out.reserve(list->size());
    for (std::size_t i = 0; i < list->size(); ++i) {
        auto reader = Reader::of((*list)[i], index_path(in.at(key), i));
        if (!reader) return Unexpected(std::move(reader.error()));
        auto item = parse(*reader);
        if (!item) return Unexpected(std::move(item.error()));
        out.push_back(std::move(*item));
    }
    return out;
}

// Points at the first offending entry or range, in document order.
[[nodiscard]] std::expected<void, CatalogError> check_consistency(const Catalog& catalog) {
    std::vector<std::pair<std::string, std::size_t>> ids;
    ids.reserve(catalog.entries.size());
    for (std::size_t i = 0; i < catalog.entries.size(); ++i) ids.emplace_back(ascii_lower(catalog.entries[i].id), i);
    std::ranges::sort(ids);
    for (std::size_t i = 1; i < ids.size(); ++i)
        if (ids[i].first == ids[i - 1].first) return malformed(index_path("entries", ids[i].second) + ".id");

    std::vector<std::size_t> order(catalog.flag_ranges.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::ranges::sort(order, {}, [&](std::size_t i) { return catalog.flag_ranges[i].versions.first; });
    for (std::size_t i = 1; i < order.size(); ++i)
        if (catalog.flag_ranges[order[i]].versions.first <= catalog.flag_ranges[order[i - 1]].versions.last)
            return malformed(index_path("flag_ranges", std::max(order[i], order[i - 1])));

    for (std::size_t i = 0; i < catalog.entries.size(); ++i) {
        const auto covers = [&](const BuildFlagRange& range) { return range.versions.contains(catalog.entries[i].version); };
        if (std::ranges::none_of(catalog.flag_ranges, covers)) return malformed(index_path("entries", i) + ".version");
    }
    return {};
}

}  // namespace

std::expected<Catalog, CatalogError> parse_catalog(std::span<const u8> body) {
    boost::system::error_code ec;
    const json::value document =
        json::parse(json::string_view(reinterpret_cast<const char*>(body.data()), body.size()), ec);
    if (ec) return malformed("$");
    auto in = Reader::of(document, "");
    if (!in) return malformed("$");

    auto schema = in->u64_at("schema");
    if (!schema) return Unexpected(std::move(schema.error()));
    if (*schema != kCatalogSchema)
        return Unexpected(CatalogError{.code = CatalogErrorCode::UnknownSchema,
                                       .schema = static_cast<u32>(std::min<u64>(*schema, std::numeric_limits<u32>::max()))});

    Catalog catalog;
    catalog.schema = kCatalogSchema;
    auto serial = in->u64_at("serial");
    if (!serial) return Unexpected(std::move(serial.error()));
    catalog.serial = *serial;

    auto generated = in->time_at("generated_unix_ms");
    if (!generated) return Unexpected(std::move(generated.error()));
    catalog.generated_at = *generated;
    auto expires = in->time_at("expires_unix_ms");
    if (!expires) return Unexpected(std::move(expires.error()));
    catalog.expires_at = *expires;

    auto entries = parse_list<CatalogEntry>(*in, "entries", parse_entry);
    if (!entries) return Unexpected(std::move(entries.error()));
    catalog.entries = std::move(*entries);

    auto ranges = parse_list<BuildFlagRange>(*in, "flag_ranges", parse_range);
    if (!ranges) return Unexpected(std::move(ranges.error()));
    catalog.flag_ranges = std::move(*ranges);

    if (auto consistent = check_consistency(catalog); !consistent) return Unexpected(std::move(consistent.error()));

    std::ranges::sort(catalog.entries, [](const CatalogEntry& a, const CatalogEntry& b) {
        if (a.version != b.version) return a.version < b.version;
        return a.id < b.id;
    });
    return catalog;
}

}  // namespace reboot::catalog
