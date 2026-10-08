#include "reboot/storage/library_document.hpp"

#include <algorithm>
#include <utility>

#include <boost/json/array.hpp>

#include "member_reader.hpp"
#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::storage {

namespace json = boost::json;

namespace {

[[nodiscard]] Result<std::string> name_from_json(const json::value& raw) {
    Result<std::string> name = string_from_json(raw);
    if (name && name->empty()) return invalid_input(msg::kInvalidValue).arg("value", "").arg("member", "name").fail();
    return name;
}

[[nodiscard]] Result<LibraryEntry> entry_from_json(const json::value& raw, const std::string& path,
                                                   std::vector<ValueIssue>& issues) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<BuildId> id = required(**object, "id", id_from_json<BuildTag>);
    if (!id) return std::unexpected(std::move(id.error()));
    Result<std::string> name = required(**object, "name", name_from_json);
    if (!name) return std::unexpected(std::move(name.error()));
    Result<NativePath> root = required(**object, "root", path_from_json);
    if (!root) return std::unexpected(std::move(root.error()));

    LibraryEntry entry{.id = *id, .name = std::move(*name), .root = std::move(*root)};
    MemberReader reader(**object, issues, path + ".");
    reader.read_optional("version", entry.version, game_version_from_json);
    reader.read_optional("changelist", entry.changelist, [](const json::value& value) {
        return u32_from_json(value).transform([](u32 number) { return Changelist{number}; });
    });
    reader.read_optional("catalog_entry", entry.catalog_entry, string_from_json);
    reader.read("added_at", entry.added_at, time_from_json);
    reader.read("needs_relocation", entry.needs_relocation, bool_from_json);
    return entry;
}

[[nodiscard]] json::object entry_to_json(const LibraryEntry& entry) {
    json::object out;
    out.emplace("id", id_to_json(entry.id));
    out.emplace("name", entry.name);
    out.emplace("root", path_to_json(entry.root));
    if (entry.version) out.emplace("version", game_version_to_json(*entry.version));
    if (entry.changelist) out.emplace("changelist", entry.changelist->value);
    if (entry.catalog_entry) out.emplace("catalog_entry", *entry.catalog_entry);
    out.emplace("added_at", time_to_json(entry.added_at));
    out.emplace("needs_relocation", entry.needs_relocation);
    return out;
}

}  // namespace

LibraryDocument LibraryDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    LibraryDocument document;
    MemberReader reader(values, issues);
    reader.read_list("builds", document.builds, [&issues](const json::value& raw, const std::string& path) {
        return entry_from_json(raw, path, issues);
    });
    reader.read_optional("client_selection", document.client_selection, id_from_json<BuildTag>);
    reader.read_optional("host_selection", document.host_selection, id_from_json<BuildTag>);
    document.unknown = reader.unknown();

    const auto known = [&document](const std::optional<BuildId>& selection) {
        const auto selected = [&selection](const LibraryEntry& entry) { return entry.id == *selection; };
        return selection && std::ranges::any_of(document.builds, selected);
    };
    if (!known(document.client_selection)) document.client_selection.reset();
    if (!known(document.host_selection)) document.host_selection.reset();
    return document;
}

json::object LibraryDocument::write() const {
    json::array builds_json;
    for (const LibraryEntry& entry : builds) builds_json.emplace_back(entry_to_json(entry));
    json::object out;
    out.emplace("builds", std::move(builds_json));
    if (client_selection) out.emplace("client_selection", id_to_json(*client_selection));
    if (host_selection) out.emplace("host_selection", id_to_json(*host_selection));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> LibraryDocument::upgrade(json::object values, u32) { return values; }

}  // namespace reboot::storage
