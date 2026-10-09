#include "reboot/identity/backend_logins_document.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/value.hpp>

#include "backend_endpoint.hpp"
#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::identity {

namespace json = boost::json;

namespace {

[[nodiscard]] std::unexpected<Diagnostic> without_endpoint() {
    return make_diag(ErrorDomain::Identity, msg::kBackendLoginWithoutEndpoint).kind(ErrorKind::InvalidInput).fail();
}

// JSON null reads as absent.
[[nodiscard]] const json::value* member(const json::object& object, std::string_view name) {
    const json::value* value = object.if_contains(json::string_view(name.data(), name.size()));
    return value != nullptr && !value->is_null() ? value : nullptr;
}

[[nodiscard]] Result<HostPort> endpoint_from_json(const json::value& raw) {
    const json::object* object = raw.if_object();
    const json::value* host = object != nullptr ? member(*object, "host") : nullptr;
    if (host == nullptr) return without_endpoint();
    Result<std::string> host_text = storage::string_from_json(*host);
    if (!host_text) return std::unexpected(std::move(host_text.error()));
    HostPort endpoint{std::move(*host_text), std::nullopt};
    if (const json::value* port = member(*object, "port")) {
        Result<Port> parsed = storage::port_from_json(*port);
        if (!parsed) return std::unexpected(std::move(parsed.error()));
        endpoint.port = *parsed;
    }
    return normalize_backend_endpoint(endpoint);
}

[[nodiscard]] Result<BackendLogin> login_from_json(const json::value& raw, const std::string& path,
                                                    std::vector<storage::ValueIssue>& issues) {
    const json::object* object = raw.if_object();
    const json::value* endpoint_json = object != nullptr ? member(*object, "endpoint") : nullptr;
    if (endpoint_json == nullptr) return without_endpoint();
    Result<HostPort> endpoint = endpoint_from_json(*endpoint_json);
    if (!endpoint) return std::unexpected(std::move(endpoint.error()));
    BackendLogin login{.endpoint = std::move(*endpoint)};
    if (const json::value* login_json = member(*object, "login")) {
        Result<std::string> text = storage::string_from_json(*login_json);
        if (!text) return std::unexpected(std::move(text.error()));
        if (text->empty()) return std::unexpected(empty_login(login.endpoint));
        login.login = std::move(*text);
    }
    if (const json::value* policy = member(*object, "policy")) {
        Result<CredentialPolicy> parsed = storage::enum_from_json<CredentialPolicy>(*policy);
        if (parsed) login.policy = *parsed;
        else issues.push_back({path + ".policy", std::move(parsed.error())});
    }
    for (const json::key_value_pair& entry : *object)
        if (entry.key() != "endpoint" && entry.key() != "login" && entry.key() != "policy")
            login.unknown.emplace(entry.key(), entry.value());
    return login;
}

[[nodiscard]] json::object endpoint_to_json(const HostPort& endpoint) {
    json::object out;
    out.emplace("host", endpoint.host);
    if (endpoint.port) out.emplace("port", storage::port_to_json(*endpoint.port));
    return out;
}

[[nodiscard]] bool is_default(const BackendLogin& login) { return login == BackendLogin{.endpoint = login.endpoint}; }

void append_unknown(json::object& out, const json::object& unknown) {
    for (const json::key_value_pair& entry : unknown)
        if (!out.contains(entry.key())) out.emplace(entry.key(), entry.value());
}

}  // namespace

BackendLoginsDocument BackendLoginsDocument::read(const json::object& values,
                                                  std::vector<storage::ValueIssue>& issues) {
    BackendLoginsDocument document;
    for (const json::key_value_pair& entry : values)
        if (entry.key() != "logins") document.unknown.emplace(entry.key(), entry.value());

    const json::value* raw = member(values, "logins");
    if (raw == nullptr) return document;
    const json::array* array = raw->if_array();
    if (array == nullptr) {
        issues.push_back({"logins", make_diag(ErrorDomain::Identity, msg::kBackendLoginsNotList)
                                                    .kind(ErrorKind::InvalidInput)
                                                    .build()});
        return document;
    }
    std::vector<HostPort> seen;
    for (std::size_t i = 0; i < array->size(); ++i) {
        const std::string path = "logins[" + std::to_string(i) + "]";
        Result<BackendLogin> login = login_from_json((*array)[i], path, issues);
        if (!login) {
            issues.push_back({path, std::move(login.error())});
            continue;
        }
        if (std::ranges::find(seen, login->endpoint) != seen.end()) {
            issues.push_back({path, make_diag(ErrorDomain::Identity, msg::kDuplicateBackendLogin)
                                        .kind(ErrorKind::InvalidInput)
                                        .arg("backend", endpoint_text(login->endpoint))
                                        .build()});
            continue;
        }
        seen.push_back(login->endpoint);
        if (!is_default(*login)) document.logins.push_back(std::move(*login));
    }
    return document;
}

json::object BackendLoginsDocument::write() const {
    json::array logins_json;
    for (const BackendLogin& login : logins) {
        if (is_default(login)) continue;
        json::object entry;
        entry.emplace("endpoint", endpoint_to_json(login.endpoint));
        if (login.login) entry.emplace("login", *login.login);
        entry.emplace("policy", storage::enum_to_json(login.policy));
        append_unknown(entry, login.unknown);
        logins_json.emplace_back(std::move(entry));
    }
    json::object out;
    out.emplace("logins", std::move(logins_json));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> BackendLoginsDocument::upgrade(json::object values, u32) { return values; }

}  // namespace reboot::identity
