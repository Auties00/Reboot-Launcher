#include "reboot/storage/accounts_document.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include <boost/json/array.hpp>

#include "member_reader.hpp"
#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace rb::storage {

namespace json = boost::json;

namespace {

[[nodiscard]] bool is_display_name(std::string_view name) noexcept {
    return name.size() >= 3 && name.size() <= 16 && std::ranges::all_of(name, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
           });
}

[[nodiscard]] bool is_tag(std::string_view tag) noexcept {
    return tag.size() == 6 &&
           std::ranges::all_of(tag, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); });
}

[[nodiscard]] Result<std::string> checked_text(const json::value& raw, std::string_view member,
                                               bool (*valid)(std::string_view) noexcept) {
    Result<std::string> text = string_from_json(raw);
    if (text && !valid(*text))
        return invalid_input(msg::kInvalidValue).arg("value", *text).arg("member", member).fail();
    return text;
}

// A bad or missing name or tag stays empty with a ValueIssue, so identity repairs the record instead of minting one.
void read_checked(MemberReader& reader, std::string_view member, std::string& out,
                  bool (*valid)(std::string_view) noexcept) {
    if (reader.find(member) == nullptr) {
        reader.issue(member, missing_member(member));
        return;
    }
    reader.read(member, out, [member, valid](const json::value& value) { return checked_text(value, member, valid); });
}

[[nodiscard]] Result<StoredAccount> record_from_json(const json::value& raw, const std::string& path,
                                                     std::vector<ValueIssue>& issues) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    MemberReader reader(**object, issues, path + ".");
    Result<AccountRecordId> id = required(reader, "record_id", id_from_json<AccountRecordTag>);
    if (!id) return std::unexpected(std::move(id.error()));
    Result<contracts::backend::AccountRole> role =
        required(reader, "role", enum_from_json<contracts::backend::AccountRole>);
    if (!role) return std::unexpected(std::move(role.error()));
    StoredAccount record{.record_id = *id, .role = *role, .tag = {}, .unknown = {}};
    read_checked(reader, "tag", record.tag, is_tag);
    record.unknown = reader.unknown();
    return record;
}

}  // namespace

AccountsDocument AccountsDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    AccountsDocument document;
    MemberReader reader(values, issues);
    read_checked(reader, "display_name", document.display_name, is_display_name);
    reader.read_list("records", document.records, [&issues](const json::value& raw, const std::string& path) {
        return record_from_json(raw, path, issues);
    });
    document.unknown = reader.unknown();
    return document;
}

json::object AccountsDocument::write() const {
    json::array records_json;
    for (const StoredAccount& record : records) {
        json::object entry;
        entry.emplace("record_id", id_to_json(record.record_id));
        entry.emplace("role", enum_to_json(record.role));
        entry.emplace("tag", record.tag);
        append_unknown(entry, record.unknown);
        records_json.emplace_back(std::move(entry));
    }
    json::object out;
    out.emplace("display_name", display_name);
    out.emplace("records", std::move(records_json));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> AccountsDocument::upgrade(json::object values, u32 from_schema) {
    if (from_schema >= 2) return values;
    json::value* raw_records = values.if_contains("records");
    json::array* records = raw_records != nullptr ? raw_records->if_array() : nullptr;
    if (records == nullptr) return values;
    // Schema 1 named each role; the client's name becomes the one name.
    std::optional<json::value> name;
    const json::value client = enum_to_json(contracts::backend::AccountRole::Client);
    for (json::value& raw : *records) {
        json::object* record = raw.if_object();
        if (record == nullptr) continue;
        const json::value* role = record->if_contains("role");
        if (const json::value* stored = record->if_contains("display_name"); !name && role && *role == client && stored)
            name = *stored;
        record->erase("display_name");
    }
    if (name) values["display_name"] = std::move(*name);
    return values;
}

}  // namespace rb::storage
