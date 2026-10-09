#include "reboot/compat/compat_document.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/value.hpp>

#include "messages.hpp"
#include "reboot/storage/json_values.hpp"

namespace rb::compat {

namespace json = boost::json;

namespace {

constexpr std::string_view kPrefixes = "prefixes";
constexpr std::string_view kRuntimes = "runtimes";

[[nodiscard]] Diagnostic record_invalid() {
    return make_diag(ErrorDomain::Compat, msg::kRecordInvalid).kind(ErrorKind::InvalidInput).build();
}

[[nodiscard]] Diagnostic duplicate(std::string_view name) {
    return make_diag(ErrorDomain::Compat, msg::kRecordDuplicate).arg("name", name).kind(ErrorKind::InvalidInput).build();
}

// JSON null reads as absent.
[[nodiscard]] const json::value* member(const json::object& object, std::string_view name) {
    const json::value* value = object.if_contains(json::string_view(name.data(), name.size()));
    return value != nullptr && !value->is_null() ? value : nullptr;
}

[[nodiscard]] Result<std::string> required_text(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr) return std::unexpected(record_invalid());
    Result<std::string> text = storage::string_from_json(*value);
    if (text && text->empty()) return std::unexpected(record_invalid());
    return text;
}

[[nodiscard]] Result<bool> optional_flag(const json::object& object, std::string_view name) {
    const json::value* value = member(object, name);
    if (value == nullptr) return false;
    return storage::bool_from_json(*value);
}

[[nodiscard]] Result<PrefixRecord> prefix_from_json(const json::value& raw) {
    const json::object* object = raw.if_object();
    if (object == nullptr) return std::unexpected(record_invalid());
    const json::value* runner = member(*object, "runner");
    if (runner == nullptr) return std::unexpected(record_invalid());
    Result<RunnerKind> kind = storage::enum_from_json<RunnerKind>(*runner);
    if (!kind) return std::unexpected(std::move(kind.error()));
    if (!wine_runtime_kind(*kind)) return std::unexpected(record_invalid());
    Result<std::string> runtime = required_text(*object, "runtime");
    if (!runtime) return std::unexpected(std::move(runtime.error()));
    Result<std::string> version = required_text(*object, "runtime_version");
    if (!version) return std::unexpected(std::move(version.error()));
    Result<bool> seeded = optional_flag(*object, "vc_runtime_seeded");
    if (!seeded) return std::unexpected(std::move(seeded.error()));
    return PrefixRecord{*kind, RuntimeId{std::move(*runtime)}, std::move(*version), *seeded};
}

[[nodiscard]] Result<SlrInstall> slr_from_json(const json::value& raw) {
    const json::object* object = raw.if_object();
    if (object == nullptr) return std::unexpected(record_invalid());
    Result<std::string> build = required_text(*object, "build");
    if (!build) return std::unexpected(std::move(build.error()));
    const json::value* installed = member(*object, "installed_at");
    if (installed == nullptr) return std::unexpected(record_invalid());
    auto at = storage::time_from_json(*installed);
    if (!at) return std::unexpected(std::move(at.error()));
    return SlrInstall{std::move(*build), *at};
}

[[nodiscard]] Result<RuntimeRecord> runtime_from_json(const json::value& raw) {
    const json::object* object = raw.if_object();
    if (object == nullptr) return std::unexpected(record_invalid());
    Result<std::string> runtime = required_text(*object, "runtime");
    if (!runtime) return std::unexpected(std::move(runtime.error()));
    Result<bool> completed = optional_flag(*object, "completed_session");
    if (!completed) return std::unexpected(std::move(completed.error()));
    RuntimeRecord record{RuntimeId{std::move(*runtime)}, *completed, std::nullopt};
    if (const json::value* slr = member(*object, "slr")) {
        Result<SlrInstall> install = slr_from_json(*slr);
        if (!install) return std::unexpected(std::move(install.error()));
        record.slr = std::move(*install);
    }
    return record;
}

// Reads one list member, dropping each record `parse` refuses or `key` already saw.
template <class Record, class Parse, class Key>
void read_records(const json::object& values, std::string_view name, Parse&& parse, Key&& key,
                  std::vector<Record>& out, std::vector<storage::ValueIssue>& issues) {
    const json::value* raw = member(values, name);
    if (raw == nullptr) return;
    const json::array* array = raw->if_array();
    if (array == nullptr) {
        issues.push_back({std::string(name), make_diag(ErrorDomain::Compat, msg::kRecordsNotList)
                                                 .arg("member", name)
                                                 .kind(ErrorKind::InvalidInput)
                                                 .build()});
        return;
    }
    for (std::size_t i = 0; i < array->size(); ++i) {
        const std::string path = std::string(name) + "[" + std::to_string(i) + "]";
        Result<Record> record = parse((*array)[i]);
        if (!record) {
            issues.push_back({path, std::move(record.error())});
            continue;
        }
        const std::string id = key(*record);
        if (std::ranges::any_of(out, [&](const Record& seen) { return key(seen) == id; })) {
            issues.push_back({path, duplicate(id)});
            continue;
        }
        out.push_back(std::move(*record));
    }
}

}  // namespace

CompatDocument CompatDocument::read(const json::object& values, std::vector<storage::ValueIssue>& issues) {
    CompatDocument document;
    for (const json::key_value_pair& entry : values)
        if (entry.key() != kPrefixes && entry.key() != kRuntimes) document.unknown.emplace(entry.key(), entry.value());
    read_records<PrefixRecord>(
        values, kPrefixes, prefix_from_json, [](const PrefixRecord& r) { return std::string(runner_name(r.kind)); },
        document.prefixes, issues);
    read_records<RuntimeRecord>(
        values, kRuntimes, runtime_from_json, [](const RuntimeRecord& r) { return r.runtime.value; }, document.runtimes,
        issues);
    return document;
}

json::object CompatDocument::write() const {
    json::array prefixes_json;
    for (const PrefixRecord& record : prefixes) {
        json::object entry;
        entry.emplace("runner", storage::enum_to_json(record.kind));
        entry.emplace("runtime", record.runtime.value);
        entry.emplace("runtime_version", record.runtime_version);
        entry.emplace("vc_runtime_seeded", record.vc_runtime_seeded);
        prefixes_json.emplace_back(std::move(entry));
    }
    json::array runtimes_json;
    for (const RuntimeRecord& record : runtimes) {
        json::object entry;
        entry.emplace("runtime", record.runtime.value);
        entry.emplace("completed_session", record.completed_session);
        if (record.slr) {
            json::object slr;
            slr.emplace("build", record.slr->build);
            slr.emplace("installed_at", storage::time_to_json(record.slr->installed_at));
            entry.emplace("slr", std::move(slr));
        }
        runtimes_json.emplace_back(std::move(entry));
    }
    json::object out;
    out.emplace(kPrefixes, std::move(prefixes_json));
    out.emplace(kRuntimes, std::move(runtimes_json));
    for (const json::key_value_pair& entry : unknown)
        if (!out.contains(entry.key())) out.emplace(entry.key(), entry.value());
    return out;
}

Result<json::object> CompatDocument::upgrade(json::object values, u32) { return values; }

}  // namespace rb::compat
