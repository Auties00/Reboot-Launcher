#include "reboot/storage/runtime_document.hpp"

#include <limits>
#include <utility>

#include <boost/json/array.hpp>

#include "member_reader.hpp"
#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace rb::storage {

namespace json = boost::json;

namespace {

[[nodiscard]] Result<Port> list_port(const json::value& raw, const std::string&) { return port_from_json(raw); }

[[nodiscard]] json::array ports_to_json(const std::vector<Port>& ports) {
    json::array out;
    for (const Port port : ports) out.emplace_back(port_to_json(port));
    return out;
}

// 0 and values past pid_t's range would make a POSIX kill() signal a whole group instead.
[[nodiscard]] Result<u32> pid_from_json(const json::value& raw) {
    Result<u32> pid = u32_from_json(raw);
    if (pid && (*pid == 0 || *pid > static_cast<u32>(std::numeric_limits<i32>::max())))
        return invalid_input(msg::kOutOfRange).arg("value", *pid).fail();
    return pid;
}

[[nodiscard]] Result<EnginePort> engine_port_from_json(const json::value& raw) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<Port> port = required(**object, "port", port_from_json);
    if (!port) return std::unexpected(std::move(port.error()));
    Result<EnginePortRole> role = required(**object, "role", enum_from_json<EnginePortRole>);
    if (!role) return std::unexpected(std::move(role.error()));
    return EnginePort{*port, *role};
}

[[nodiscard]] Result<RecordedProcess> child_from_json(const json::value& raw, const std::string& path,
                                                      std::vector<ValueIssue>& issues) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<u32> pid = required(**object, "pid", pid_from_json);
    if (!pid) return std::unexpected(std::move(pid.error()));
    Result<std::chrono::system_clock::time_point> created = required(**object, "created", time_from_json);
    if (!created) return std::unexpected(std::move(created.error()));
    Result<ChildRole> role = required(**object, "role", enum_from_json<ChildRole>);
    if (!role) return std::unexpected(std::move(role.error()));
    RecordedProcess child{.pid = *pid, .created = *created, .role = *role};
    MemberReader reader(**object, issues, path + ".");
    reader.read_optional("session", child.session, id_from_json<SessionTag>);
    reader.read_list("ports", child.ports, list_port);
    return child;
}

}  // namespace

RuntimeDocument RuntimeDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    RuntimeDocument document;
    MemberReader reader(values, issues);
    reader.read("engine_pid", document.engine_pid, u32_from_json);
    reader.read("engine_created", document.engine_created, time_from_json);
    reader.read("engine_build", document.engine_build, string_from_json);
    reader.read("origin", document.origin, enum_from_json<contracts::ipc::EngineOrigin>);
    reader.read("endpoint", document.endpoint, path_from_json);
    reader.read_list("engine_ports", document.engine_ports,
                     [](const json::value& raw, const std::string&) { return engine_port_from_json(raw); });
    reader.read_list("children", document.children, [&issues](const json::value& raw, const std::string& path) {
        return child_from_json(raw, path, issues);
    });
    document.unknown = reader.unknown();
    return document;
}

json::object RuntimeDocument::write() const {
    json::array engine_ports_json;
    for (const EnginePort& engine_port : engine_ports) {
        json::object entry;
        entry.emplace("port", port_to_json(engine_port.port));
        entry.emplace("role", enum_to_json(engine_port.role));
        engine_ports_json.emplace_back(std::move(entry));
    }
    json::array children_json;
    for (const RecordedProcess& child : children) {
        json::object entry;
        entry.emplace("pid", child.pid);
        entry.emplace("created", time_to_json(child.created));
        entry.emplace("role", enum_to_json(child.role));
        if (child.session) entry.emplace("session", id_to_json(*child.session));
        entry.emplace("ports", ports_to_json(child.ports));
        children_json.emplace_back(std::move(entry));
    }

    json::object out;
    out.emplace("engine_pid", engine_pid);
    out.emplace("engine_created", time_to_json(engine_created));
    out.emplace("engine_build", engine_build);
    out.emplace("origin", enum_to_json(origin));
    if (!endpoint.empty()) out.emplace("endpoint", path_to_json(endpoint));
    out.emplace("engine_ports", std::move(engine_ports_json));
    out.emplace("children", std::move(children_json));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> RuntimeDocument::upgrade(json::object values, u32) { return values; }

}  // namespace rb::storage
