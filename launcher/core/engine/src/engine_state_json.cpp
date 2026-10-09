#include "engine_state_json.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>

#include "reboot/foundation/net_types.hpp"
#include "reboot/storage/json_values.hpp"

namespace rb::engine {

namespace json = boost::json;

namespace {

constexpr std::array<std::string_view, 2> kScopeNames{"all", "installed"};
constexpr std::array<std::string_view, 3> kPasswordNames{"any", "without", "only"};
constexpr std::array<std::string_view, 3> kSortNames{"players", "newest", "name"};
constexpr std::array<std::string_view, 2> kMethodNames{"upnp", "nat_pmp"};

template <class E, std::size_t N>
[[nodiscard]] E named(const json::object& object, std::string_view key, const std::array<std::string_view, N>& names, E fallback) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) return fallback;
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == std::string_view(value->get_string())) return static_cast<E>(i);
    return fallback;
}

[[nodiscard]] std::optional<std::string> text(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) return std::nullopt;
    return std::string(value->get_string());
}

[[nodiscard]] std::optional<u64> number(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr) return std::nullopt;
    Result<u64> parsed = storage::u64_from_json(*value);
    if (!parsed) return std::nullopt;
    return *parsed;
}

[[nodiscard]] std::optional<Port> port(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr) return std::nullopt;
    Result<Port> parsed = storage::port_from_json(*value);
    if (!parsed) return std::nullopt;
    return *parsed;
}

}  // namespace

json::value join_target_to_json(const std::optional<browser::JoinTarget>& target) {
    if (!target) return nullptr;
    json::object out;
    if (const auto* server = std::get_if<browser::ServerTarget>(&target->target)) {
        json::object entry;
        entry["id"] = storage::id_to_json(server->id);
        entry["name"] = server->name;
        entry["author"] = server->author;
        out["server"] = std::move(entry);
    } else {
        const auto& address = std::get<browser::AddressTarget>(target->target);
        json::object entry;
        entry["text"] = address.text;
        entry["host"] = address.address.host;
        if (address.address.port) entry["port"] = storage::port_to_json(*address.address.port);
        out["address"] = std::move(entry);
    }
    return out;
}

std::optional<browser::JoinTarget> join_target_from_json(const json::value* value) {
    if (value == nullptr || !value->is_object()) return std::nullopt;
    const json::object& object = value->get_object();
    if (const json::value* server = object.if_contains("server"); server && server->is_object()) {
        const json::object& entry = server->get_object();
        const json::value* id = entry.if_contains("id");
        if (id == nullptr) return std::nullopt;
        Result<ServerId> parsed = storage::id_from_json<ServerTag>(*id);
        if (!parsed) return std::nullopt;
        return browser::JoinTarget{browser::ServerTarget{*parsed, text(entry, "name").value_or(""), text(entry, "author").value_or("")}};
    }
    if (const json::value* address = object.if_contains("address"); address && address->is_object()) {
        const json::object& entry = address->get_object();
        std::optional<std::string> host = text(entry, "host");
        if (!host || host->empty()) return std::nullopt;
        return browser::JoinTarget{browser::AddressTarget{text(entry, "text").value_or(*host), HostPort{*host, port(entry, "port")}}};
    }
    return std::nullopt;
}

json::value browse_choices_to_json(const browser::BrowseChoices& choices) {
    json::object out;
    out["versions"] = kScopeNames[static_cast<std::size_t>(choices.versions)];
    out["password"] = kPasswordNames[static_cast<std::size_t>(choices.password)];
    out["region"] = static_cast<std::uint64_t>(choices.region);
    out["sort"] = kSortNames[static_cast<std::size_t>(choices.sort)];
    return out;
}

browser::BrowseChoices browse_choices_from_json(const json::value* value) {
    browser::BrowseChoices choices;
    if (value == nullptr || !value->is_object()) return choices;
    const json::object& object = value->get_object();
    choices.versions = named(object, "versions", kScopeNames, browser::VersionScope::All);
    choices.password = named(object, "password", kPasswordNames, browser::PasswordFilter::Any);
    if (const std::optional<u64> region = number(object, "region");
        region && *region <= static_cast<u64>(browser::Region::SouthAmerica))
        choices.region = static_cast<browser::Region>(*region);
    choices.sort = named(object, "sort", kSortNames, browser::ServerSort::Players);
    return choices;
}

json::value mapping_records_to_json(const std::vector<net::MappingRecord>& records) {
    json::array out;
    for (const net::MappingRecord& record : records) {
        json::object entry;
        entry["session"] = storage::id_to_json(record.session);
        entry["internal"] = storage::port_to_json(record.mapping.internal);
        entry["external"] = storage::port_to_json(record.mapping.external);
        entry["method"] = kMethodNames[static_cast<std::size_t>(record.mapping.method)];
        entry["lease_s"] = static_cast<std::int64_t>(record.mapping.lease.count());
        entry["lan"] = record.mapping.lan_address.to_string();
        out.push_back(std::move(entry));
    }
    return out;
}

std::vector<net::MappingRecord> mapping_records_from_json(const json::value* value) {
    std::vector<net::MappingRecord> records;
    if (value == nullptr || !value->is_array()) return records;
    for (const json::value& item : value->get_array()) {
        if (!item.is_object()) continue;
        const json::object& entry = item.get_object();
        const json::value* session = entry.if_contains("session");
        const std::optional<Port> internal = port(entry, "internal");
        const std::optional<Port> external = port(entry, "external");
        const std::optional<std::string> lan = text(entry, "lan");
        if (session == nullptr || !internal || !external || !lan) continue;
        Result<SessionId> id = storage::id_from_json<SessionTag>(*session);
        const std::optional<IpAddress> address = IpAddress::parse(*lan);
        if (!id || !address) continue;
        net::MappingRecord record;
        record.session = *id;
        record.mapping.internal = *internal;
        record.mapping.external = *external;
        record.mapping.method = named(entry, "method", kMethodNames, net::MappingMethod::Upnp);
        record.mapping.lease = std::chrono::seconds{static_cast<i64>(number(entry, "lease_s").value_or(0))};
        record.mapping.lan_address = *address;
        records.push_back(std::move(record));
    }
    return records;
}

json::value serials_to_json(const SerialFloors& floors) {
    json::object out;
    out["catalog"] = floors.catalog;
    out["manifest"] = floors.manifest;
    return out;
}

SerialFloors serials_from_json(const json::value* value) {
    SerialFloors floors;
    if (value == nullptr || !value->is_object()) return floors;
    floors.catalog = number(value->get_object(), "catalog").value_or(0);
    floors.manifest = number(value->get_object(), "manifest").value_or(0);
    return floors;
}

}  // namespace rb::engine
