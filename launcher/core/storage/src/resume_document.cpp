#include "reboot/storage/resume_document.hpp"

#include <utility>

#include <boost/json/array.hpp>

#include "member_reader.hpp"
#include "reboot/storage/json_values.hpp"

namespace rb::storage {

namespace json = boost::json;

namespace {

[[nodiscard]] Result<PendingUpdate> pending_update_from_json(const json::value& raw) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<SemVer> from = required(**object, "from", semver_from_json);
    if (!from) return std::unexpected(std::move(from.error()));
    Result<SemVer> to = required(**object, "to", semver_from_json);
    if (!to) return std::unexpected(std::move(to.error()));
    Result<u32> attempts = required(**object, "attempts", u32_from_json);
    if (!attempts) return std::unexpected(std::move(attempts.error()));
    return PendingUpdate{std::move(*from), std::move(*to), *attempts};
}

}  // namespace

ResumeDocument ResumeDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    ResumeDocument document;
    MemberReader reader(values, issues);
    reader.read("origin", document.origin, enum_from_json<contracts::ipc::EngineOrigin>);
    reader.read_list("reopen_clients", document.reopen_clients, [](const json::value& raw, const std::string&) {
        return enum_from_json<contracts::ipc::ClientKind>(raw);
    });
    reader.read_list("relaunch_hosts", document.relaunch_hosts,
                     [](const json::value& raw, const std::string&) { return id_from_json<HostProfileTag>(raw); });
    reader.read_optional("payload_version", document.payload_version, semver_from_json);
    reader.read_list("runtime_ids", document.runtime_ids,
                     [](const json::value& raw, const std::string&) { return string_from_json(raw); });
    reader.read_optional("pending_update", document.pending_update, pending_update_from_json);
    document.unknown = reader.unknown();
    return document;
}

json::object ResumeDocument::write() const {
    json::array clients_json;
    for (const contracts::ipc::ClientKind client : reopen_clients) clients_json.emplace_back(enum_to_json(client));
    json::array hosts_json;
    for (const HostProfileId& host : relaunch_hosts) hosts_json.emplace_back(id_to_json(host));
    json::array runtime_ids_json;
    for (const std::string& runtime_id : runtime_ids) runtime_ids_json.emplace_back(runtime_id);

    json::object out;
    out.emplace("origin", enum_to_json(origin));
    out.emplace("reopen_clients", std::move(clients_json));
    out.emplace("relaunch_hosts", std::move(hosts_json));
    if (payload_version) out.emplace("payload_version", semver_to_json(*payload_version));
    out.emplace("runtime_ids", std::move(runtime_ids_json));
    if (pending_update) {
        json::object update;
        update.emplace("from", semver_to_json(pending_update->from));
        update.emplace("to", semver_to_json(pending_update->to));
        update.emplace("attempts", pending_update->attempts);
        out.emplace("pending_update", std::move(update));
    }
    append_unknown(out, unknown);
    return out;
}

Result<json::object> ResumeDocument::upgrade(json::object values, u32) { return values; }

}  // namespace rb::storage
