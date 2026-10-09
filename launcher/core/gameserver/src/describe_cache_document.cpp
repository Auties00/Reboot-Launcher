#include "reboot/gameserver/describe_cache_document.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/value.hpp>

#include "messages.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::gameserver {

namespace {

namespace json = boost::json;
namespace gs = contracts::game_server;

constexpr std::array<std::string_view, 2> kSocketRoleNames{"game", "beacon"};

[[nodiscard]] std::unexpected<Diagnostic> invalid(std::string_view member) {
    return make_diag(ErrorDomain::GameServer, msg::kCacheEntryInvalid)
        .arg("member", member)
        .kind(ErrorKind::InvalidInput)
        .fail();
}

// JSON null reads as absent.
[[nodiscard]] const json::value* member(const json::object& object, std::string_view name) {
    const json::value* value = object.if_contains(json::string_view(name.data(), name.size()));
    return value != nullptr && !value->is_null() ? value : nullptr;
}

[[nodiscard]] Result<const json::object*> object_member(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr || !value->is_object()) return invalid(name);
    return &value->get_object();
}

[[nodiscard]] Result<const json::array*> array_member(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr || !value->is_array()) return invalid(name);
    return &value->get_array();
}

[[nodiscard]] Result<std::string> string_member(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr) return invalid(name);
    return storage::string_from_json(*value);
}

[[nodiscard]] Result<u32> u32_member(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr) return invalid(name);
    Result<u64> number = storage::u64_from_json(*value);
    if (!number) return std::unexpected(std::move(number.error()));
    if (*number > std::numeric_limits<u32>::max()) return invalid(name);
    return static_cast<u32>(*number);
}

[[nodiscard]] Result<bool> bool_member(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr) return invalid(name);
    return storage::bool_from_json(*value);
}

[[nodiscard]] std::optional<u8> hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
    return std::nullopt;
}

[[nodiscard]] Result<Sha256Digest> digest_member(const json::object& object) {
    Result<std::string> text = string_member(object, "sha256");
    if (!text) return std::unexpected(std::move(text.error()));
    Sha256Digest digest{};
    if (text->size() != digest.size() * 2) return invalid("sha256");
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const std::optional<u8> high = hex_digit((*text)[2 * i]);
        const std::optional<u8> low = hex_digit((*text)[2 * i + 1]);
        if (!high || !low) return invalid("sha256");
        digest[i] = static_cast<u8>(*high << 4 | *low);
    }
    return digest;
}

[[nodiscard]] Result<gs::VersionSupport> support_from_json(const json::value& raw) {
    if (!raw.is_object()) return invalid("supports");
    const json::object& object = raw.get_object();
    gs::VersionSupport support;
    Result<std::string> min = string_member(object, "version_min");
    if (!min) return std::unexpected(std::move(min.error()));
    Result<std::string> max = string_member(object, "version_max");
    if (!max) return std::unexpected(std::move(max.error()));
    Result<const json::array*> ranges = array_member(object, "cl_ranges");
    if (!ranges) return std::unexpected(std::move(ranges.error()));
    support.version_min = std::move(*min);
    support.version_max = std::move(*max);
    for (const json::value& range_json : **ranges) {
        if (!range_json.is_object()) return invalid("cl_ranges");
        Result<u32> first = u32_member(range_json.get_object(), "first");
        if (!first) return std::unexpected(std::move(first.error()));
        Result<u32> last = u32_member(range_json.get_object(), "last");
        if (!last) return std::unexpected(std::move(last.error()));
        support.cl_ranges.push_back(gs::ClRange{*first, *last});
    }
    return support;
}

[[nodiscard]] Result<gs::ServerCapabilities> capabilities_from_json(const json::object& object) {
    gs::ServerCapabilities capabilities;
    Result<bool> reset = bool_member(object, "in_process_reset");
    if (!reset) return std::unexpected(std::move(reset.error()));
    Result<bool> backend = bool_member(object, "needs_backend");
    if (!backend) return std::unexpected(std::move(backend.error()));
    Result<const json::array*> commands = array_member(object, "operator_commands");
    if (!commands) return std::unexpected(std::move(commands.error()));
    capabilities.in_process_reset = *reset;
    capabilities.needs_backend = *backend;
    for (const json::value& command : **commands) {
        Result<std::string> name = storage::string_from_json(command);
        if (!name) return std::unexpected(std::move(name.error()));
        capabilities.operator_commands.push_back(std::move(*name));
    }
    return capabilities;
}

[[nodiscard]] Result<GameServerDescription> description_from_json(const json::object& object) {
    GameServerDescription description;
    Result<u32> protocol = u32_member(object, "protocol");
    if (!protocol) return std::unexpected(std::move(protocol.error()));
    Result<std::string> build = string_member(object, "build");
    if (!build) return std::unexpected(std::move(build.error()));
    Result<const json::array*> supports = array_member(object, "supports");
    if (!supports) return std::unexpected(std::move(supports.error()));
    Result<const json::array*> sockets = array_member(object, "sockets");
    if (!sockets) return std::unexpected(std::move(sockets.error()));
    Result<const json::object*> capabilities_json = object_member(object, "capabilities");
    if (!capabilities_json) return std::unexpected(std::move(capabilities_json.error()));

    description.protocol = *protocol;
    description.build = std::move(*build);
    for (const json::value& support_json : **supports) {
        Result<gs::VersionSupport> support = support_from_json(support_json);
        if (!support) return std::unexpected(std::move(support.error()));
        description.supports.push_back(std::move(*support));
    }
    for (const json::value& socket_json : **sockets) {
        Result<std::size_t> role = storage::name_index_from_json(socket_json, kSocketRoleNames);
        if (!role) return std::unexpected(std::move(role.error()));
        description.sockets.push_back(gs::SocketSpec{static_cast<gs::SocketRole>(*role)});
    }
    Result<gs::ServerCapabilities> capabilities = capabilities_from_json(**capabilities_json);
    if (!capabilities) return std::unexpected(std::move(capabilities.error()));
    description.capabilities = std::move(*capabilities);
    return description;
}

[[nodiscard]] Result<CachedDescription> entry_from_json(const json::value& raw) {
    if (!raw.is_object()) return invalid("entries");
    const json::object& object = raw.get_object();
    Result<Sha256Digest> digest = digest_member(object);
    if (!digest) return std::unexpected(std::move(digest.error()));
    const json::value* described_at_json = member(object, "described_at");
    if (described_at_json == nullptr) return invalid("described_at");
    Result<std::chrono::system_clock::time_point> described_at = storage::time_from_json(*described_at_json);
    if (!described_at) return std::unexpected(std::move(described_at.error()));
    Result<const json::object*> description_json = object_member(object, "description");
    if (!description_json) return std::unexpected(std::move(description_json.error()));
    Result<GameServerDescription> description = description_from_json(**description_json);
    if (!description) return std::unexpected(std::move(description.error()));
    return CachedDescription{*digest, *described_at, std::move(*description)};
}

[[nodiscard]] json::object description_to_json(const GameServerDescription& description) {
    json::array supports;
    for (const gs::VersionSupport& support : description.supports) {
        json::array ranges;
        for (const gs::ClRange& range : support.cl_ranges) {
            json::object range_json;
            range_json.emplace("first", range.first);
            range_json.emplace("last", range.last);
            ranges.emplace_back(std::move(range_json));
        }
        json::object support_json;
        support_json.emplace("version_min", support.version_min);
        support_json.emplace("version_max", support.version_max);
        support_json.emplace("cl_ranges", std::move(ranges));
        supports.emplace_back(std::move(support_json));
    }
    json::array sockets;
    for (const gs::SocketSpec& socket : description.sockets)
        sockets.emplace_back(storage::name_to_json(kSocketRoleNames[static_cast<std::size_t>(socket.role)]));
    json::array commands;
    for (const std::string& command : description.capabilities.operator_commands) commands.emplace_back(command);
    json::object capabilities;
    capabilities.emplace("in_process_reset", description.capabilities.in_process_reset);
    capabilities.emplace("needs_backend", description.capabilities.needs_backend);
    capabilities.emplace("operator_commands", std::move(commands));

    json::object out;
    out.emplace("protocol", description.protocol);
    out.emplace("build", description.build);
    out.emplace("supports", std::move(supports));
    out.emplace("sockets", std::move(sockets));
    out.emplace("capabilities", std::move(capabilities));
    return out;
}

}  // namespace

const CachedDescription* DescribeCacheDocument::find(const Sha256Digest& sha256) const noexcept {
    const auto found = std::ranges::find(entries, sha256, &CachedDescription::sha256);
    return found != entries.end() ? &*found : nullptr;
}

void DescribeCacheDocument::put(CachedDescription entry) {
    erase(entry.sha256);
    entries.push_back(std::move(entry));
    while (entries.size() > kMaxEntries)
        entries.erase(std::ranges::min_element(entries, {}, &CachedDescription::described_at));
}

void DescribeCacheDocument::erase(const Sha256Digest& sha256) {
    std::erase_if(entries, [&](const CachedDescription& entry) { return entry.sha256 == sha256; });
}

DescribeCacheDocument DescribeCacheDocument::read(const json::object& values,
                                                  std::vector<storage::ValueIssue>& issues) {
    DescribeCacheDocument document;
    for (const json::key_value_pair& entry : values)
        if (entry.key() != "entries") document.unknown.emplace(entry.key(), entry.value());

    const json::value* raw = member(values, "entries");
    if (raw == nullptr) return document;
    if (!raw->is_array()) {
        issues.push_back({"entries", invalid("entries").error()});
        return document;
    }
    const json::array& array = raw->get_array();
    for (std::size_t i = 0; i < array.size(); ++i) {
        Result<CachedDescription> entry = entry_from_json(array[i]);
        if (entry) document.put(std::move(*entry));
        else issues.push_back({"entries[" + std::to_string(i) + "]", std::move(entry.error())});
    }
    return document;
}

json::object DescribeCacheDocument::write() const {
    json::array entries_json;
    for (const CachedDescription& entry : entries) {
        json::object entry_json;
        entry_json.emplace("sha256", to_hex(entry.sha256));
        entry_json.emplace("described_at", storage::time_to_json(entry.described_at));
        entry_json.emplace("description", description_to_json(entry.description));
        entries_json.emplace_back(std::move(entry_json));
    }
    json::object out;
    out.emplace("entries", std::move(entries_json));
    for (const json::key_value_pair& entry : unknown)
        if (!out.contains(entry.key())) out.emplace(entry.key(), entry.value());
    return out;
}

Result<json::object> DescribeCacheDocument::upgrade(json::object values, u32) { return values; }

}  // namespace reboot::gameserver
