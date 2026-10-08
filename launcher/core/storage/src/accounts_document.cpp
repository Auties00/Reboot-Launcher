#include "reboot/storage/accounts_document.hpp"

#include <algorithm>
#include <utility>

#include <boost/json/array.hpp>

#include "member_reader.hpp"
#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace reboot::storage {

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

[[nodiscard]] Result<AccountRecord> record_from_json(const json::value& raw) {
    Result<const json::object*> object = object_from_json(raw);
    if (!object) return std::unexpected(std::move(object.error()));
    Result<AccountRecordId> id = required(**object, "record_id", id_from_json<AccountRecordTag>);
    if (!id) return std::unexpected(std::move(id.error()));
    Result<contracts::backend::AccountRole> role =
        required(**object, "role", enum_from_json<contracts::backend::AccountRole>);
    if (!role) return std::unexpected(std::move(role.error()));
    Result<std::string> name = required(**object, "display_name", [](const json::value& value) {
        return checked_text(value, "display_name", is_display_name);
    });
    if (!name) return std::unexpected(std::move(name.error()));
    Result<std::string> tag =
        required(**object, "tag", [](const json::value& value) { return checked_text(value, "tag", is_tag); });
    if (!tag) return std::unexpected(std::move(tag.error()));
    return AccountRecord{*id, *role, std::move(*name), std::move(*tag)};
}

}  // namespace

AccountsDocument AccountsDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    AccountsDocument document;
    MemberReader reader(values, issues);
    reader.read_list("records", document.records,
                     [](const json::value& raw, const std::string&) { return record_from_json(raw); });
    document.unknown = reader.unknown();
    return document;
}

json::object AccountsDocument::write() const {
    json::array records_json;
    for (const AccountRecord& record : records) {
        json::object entry;
        entry.emplace("record_id", id_to_json(record.record_id));
        entry.emplace("role", enum_to_json(record.role));
        entry.emplace("display_name", record.display_name);
        entry.emplace("tag", record.tag);
        records_json.emplace_back(std::move(entry));
    }
    json::object out;
    out.emplace("records", std::move(records_json));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> AccountsDocument::upgrade(json::object values, u32) { return values; }

}  // namespace reboot::storage
