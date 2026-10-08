#include "reboot/storage/state_document.hpp"

#include <utility>

#include <boost/json/array.hpp>

#include "member_reader.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::storage {

namespace json = boost::json;

namespace {

[[nodiscard]] Result<std::string> list_string(const json::value& raw, const std::string&) {
    return string_from_json(raw);
}

[[nodiscard]] json::array strings_to_json(const std::vector<std::string>& strings) {
    json::array out;
    for (const std::string& text : strings) out.emplace_back(text);
    return out;
}

[[nodiscard]] Result<ShellOnboarding> onboarding_from_json(const json::value& raw, const std::string& path,
                                                           std::vector<ValueIssue>& issues) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<ShellName> shell = required(**object, "shell", [](const json::value& value) {
        return string_from_json(value).and_then([](const std::string& text) { return ShellName::parse(text); });
    });
    if (!shell) return std::unexpected(std::move(shell.error()));
    ShellOnboarding onboarding{.shell = std::move(*shell)};
    MemberReader reader(**object, issues, path + ".");
    reader.read("completed", onboarding.completed, bool_from_json);
    reader.read_list("completed_steps", onboarding.completed_steps, list_string);
    return onboarding;
}

[[nodiscard]] Result<UpstreamTlsMemory> upstream_from_json(const json::value& raw, const std::string& path,
                                                           std::vector<ValueIssue>& issues) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<std::string> host = required(**object, "host", string_from_json);
    if (!host) return std::unexpected(std::move(host.error()));
    UpstreamTlsMemory memory{.host = std::move(*host)};
    MemberReader reader(**object, issues, path + ".");
    reader.read("https_seen", memory.https_seen, bool_from_json);
    reader.read("http_acknowledged", memory.http_acknowledged, bool_from_json);
    return memory;
}

}  // namespace

StateDocument StateDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    StateDocument document;
    MemberReader reader(values, issues);
    reader.read_list("onboarding", document.onboarding, [&issues](const json::value& raw, const std::string& path) {
        return onboarding_from_json(raw, path, issues);
    });
    reader.read_list("dismissed_notices", document.dismissed_notices, list_string);
    reader.read_list("upstream_tls", document.upstream_tls, [&issues](const json::value& raw, const std::string& path) {
        return upstream_from_json(raw, path, issues);
    });
    reader.read_list("declined_integrations", document.declined_integrations,
                     [](const json::value& raw, const std::string&) {
                         return enum_from_json<ports::IntegrationKind>(raw);
                     });
    reader.read_optional("last_run_version", document.last_run_version, semver_from_json);
    document.unknown = reader.unknown();
    return document;
}

json::object StateDocument::write() const {
    json::array onboarding_json;
    for (const ShellOnboarding& shell : onboarding) {
        json::object entry;
        entry.emplace("shell", shell.shell.value);
        entry.emplace("completed", shell.completed);
        entry.emplace("completed_steps", strings_to_json(shell.completed_steps));
        onboarding_json.emplace_back(std::move(entry));
    }
    json::array upstream_json;
    for (const UpstreamTlsMemory& memory : upstream_tls) {
        json::object entry;
        entry.emplace("host", memory.host);
        entry.emplace("https_seen", memory.https_seen);
        entry.emplace("http_acknowledged", memory.http_acknowledged);
        upstream_json.emplace_back(std::move(entry));
    }
    json::array declined_json;
    for (const ports::IntegrationKind kind : declined_integrations) declined_json.emplace_back(enum_to_json(kind));

    json::object out;
    out.emplace("onboarding", std::move(onboarding_json));
    out.emplace("dismissed_notices", strings_to_json(dismissed_notices));
    out.emplace("upstream_tls", std::move(upstream_json));
    out.emplace("declined_integrations", std::move(declined_json));
    if (last_run_version) out.emplace("last_run_version", semver_to_json(*last_run_version));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> StateDocument::upgrade(json::object values, u32) { return values; }

}  // namespace reboot::storage
