#include <utility>

#include <boost/json/object.hpp>

#include "member_reader.hpp"
#include "reboot/storage/json_values.hpp"
#include "reboot/storage/key.hpp"

namespace rb::storage {

namespace json = boost::json;

namespace {

[[nodiscard]] json::object endpoint_to_json(const HostPort& endpoint) {
    json::object out;
    out.emplace("host", endpoint.host);
    if (endpoint.port) out.emplace("port", port_to_json(*endpoint.port));
    return out;
}

[[nodiscard]] Result<HostPort> endpoint_from_json(const json::value& raw) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    const json::value* host = (*object)->if_contains("host");
    if (host == nullptr) return std::unexpected(missing_member("host"));
    Result<std::string> host_text = string_from_json(*host);
    if (!host_text) return std::unexpected(std::move(host_text.error()));
    HostPort endpoint{std::move(*host_text), std::nullopt};
    if (const json::value* port = (*object)->if_contains("port")) {
        Result<Port> parsed = port_from_json(*port);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        endpoint.port = *parsed;
    }
    return endpoint;
}

[[nodiscard]] Result<std::optional<HostPort>> xmpp_from_json(const json::object& address) {
    const json::value* xmpp = address.if_contains("xmpp");
    if (xmpp == nullptr || xmpp->is_null()) return std::optional<HostPort>{};
    return endpoint_from_json(*xmpp).transform([](HostPort endpoint) { return std::optional<HostPort>(endpoint); });
}

[[nodiscard]] Result<LocalBackendAddress> local_from_json(const json::value& raw) {
    Result<HostPort> endpoint = endpoint_from_json(raw);
    if (!endpoint) return std::unexpected(std::move(endpoint.error()));
    Result<std::optional<HostPort>> xmpp = xmpp_from_json(raw.get_object());
    if (!xmpp) return std::unexpected(std::move(xmpp.error()));
    return LocalBackendAddress{std::move(*endpoint), std::move(*xmpp)};
}

[[nodiscard]] Result<RemoteBackendAddress> remote_from_json(const json::value& raw) {
    Result<HostPort> endpoint = endpoint_from_json(raw);
    if (!endpoint) return std::unexpected(std::move(endpoint.error()));
    const json::object& object = raw.get_object();
    RemoteBackendAddress remote{.scheme = std::nullopt, .endpoint = std::move(*endpoint), .xmpp = std::nullopt};
    if (const json::value* scheme = object.if_contains("scheme"); scheme != nullptr && !scheme->is_null()) {
        Result<BackendScheme> parsed = enum_from_json<BackendScheme>(*scheme);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        remote.scheme = *parsed;
    }
    Result<std::optional<HostPort>> xmpp = xmpp_from_json(object);
    if (!xmpp) return std::unexpected(std::move(xmpp.error()));
    remote.xmpp = std::move(*xmpp);
    return remote;
}

}  // namespace

json::value SettingCodec<bool>::encode(bool value) { return value; }

Result<bool> SettingCodec<bool>::decode(const json::value& raw) { return bool_from_json(raw); }

json::value SettingCodec<std::string>::encode(const std::string& value) { return json::string(value); }

Result<std::string> SettingCodec<std::string>::decode(const json::value& raw) { return string_from_json(raw); }

json::value SettingCodec<std::optional<NativePath>>::encode(const std::optional<NativePath>& value) {
    if (!value) return nullptr;
    return path_to_json(*value);
}

Result<std::optional<NativePath>> SettingCodec<std::optional<NativePath>>::decode(const json::value& raw) {
    if (raw.is_null()) return std::optional<NativePath>{};
    return path_from_json(raw).transform([](NativePath path) { return std::optional<NativePath>(std::move(path)); });
}

json::value SettingCodec<ConsoleKey>::encode(const ConsoleKey& value) { return json::string(value.name); }

Result<ConsoleKey> SettingCodec<ConsoleKey>::decode(const json::value& raw) {
    return string_from_json(raw).transform([](std::string name) { return ConsoleKey{std::move(name)}; });
}

json::value SettingCodec<BackendTarget>::encode(const BackendTarget& value) {
    json::object out;
    out.emplace("kind", enum_to_json(value.kind));
    json::object local = endpoint_to_json(value.local.endpoint);
    if (value.local.xmpp) local.emplace("xmpp", endpoint_to_json(*value.local.xmpp));
    out.emplace("local", std::move(local));
    if (value.remote) {
        json::object remote;
        if (value.remote->scheme) remote.emplace("scheme", enum_to_json(*value.remote->scheme));
        for (const json::key_value_pair& member : endpoint_to_json(value.remote->endpoint))
            remote.emplace(member.key(), member.value());
        if (value.remote->xmpp) remote.emplace("xmpp", endpoint_to_json(*value.remote->xmpp));
        out.emplace("remote", std::move(remote));
    }
    return out;
}

Result<BackendTarget> SettingCodec<BackendTarget>::decode(const json::value& raw) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    const json::value* kind = (*object)->if_contains("kind");
    if (kind == nullptr) return std::unexpected(missing_member("kind"));
    Result<BackendKind> parsed_kind = enum_from_json<BackendKind>(*kind);
    if (!parsed_kind) return std::unexpected(std::move(parsed_kind.error()));

    BackendTarget target{.kind = *parsed_kind, .local = {}, .remote = std::nullopt};
    if (const json::value* local = (*object)->if_contains("local")) {
        Result<LocalBackendAddress> parsed = local_from_json(*local);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        target.local = std::move(*parsed);
    }
    if (const json::value* remote = (*object)->if_contains("remote"); remote != nullptr && !remote->is_null()) {
        Result<RemoteBackendAddress> parsed = remote_from_json(*remote);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        target.remote = std::move(*parsed);
    }
    return target;
}

}  // namespace rb::storage
