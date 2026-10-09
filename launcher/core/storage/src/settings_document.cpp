#include "reboot/storage/settings_document.hpp"

#include <utility>

#include "member_reader.hpp"
#include "reboot/storage/key.hpp"
#include "reboot/storage/settings_keys.hpp"

namespace rb::storage {

namespace json = boost::json;

SettingsDocument SettingsDocument::read(const json::object& values, std::vector<ValueIssue>& issues) {
    SettingsDocument document;
    MemberReader reader(values, issues);
    for (const AnyKey* key : keys::kAll) {
        const json::value* raw = reader.find(key->spec().id);
        if (raw == nullptr) continue;
        Result<void> stored = key->decode_into(document.values, *raw);
        if (!stored) reader.issue(key->spec().id, std::move(stored.error()));
    }
    document.unknown = reader.unknown();
    return document;
}

json::object SettingsDocument::write() const {
    json::object out;
    for (const AnyKey* key : keys::kAll) out.emplace(key->spec().id, key->encode(values));
    append_unknown(out, unknown);
    return out;
}

Result<json::object> SettingsDocument::upgrade(json::object values, u32) { return values; }

}  // namespace rb::storage
