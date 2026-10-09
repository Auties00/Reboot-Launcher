#include "store_index.hpp"

#include <limits>
#include <string>
#include <utility>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include "json_fields.hpp"

namespace reboot::components {

namespace {

namespace json = boost::json;
using json_fields::index_path;
using json_fields::malformed;
using json_fields::Reader;

constexpr u64 kIndexSchema = 1;

[[nodiscard]] Result<Reader> sub_object(const Reader& in, std::string_view key) {
    auto value = in.required(key);
    if (!value) return std::unexpected(std::move(value.error()));
    return Reader::of(**value, in.at(key));
}

[[nodiscard]] Result<StoreEntry> read_payload(const Reader& in) {
    StoreEntry entry;
    auto version = in.nonempty_string_at("version");
    if (!version) return std::unexpected(std::move(version.error()));
    entry.ref = ComponentRef{ComponentKind::Payload, std::string(kPayloadComponentId), std::move(*version)};
    auto abi = in.u64_at("payload_abi");
    if (!abi) return std::unexpected(std::move(abi.error()));
    if (*abi > std::numeric_limits<u16>::max()) return std::unexpected(malformed(in.at("payload_abi")));
    entry.payload_abi = static_cast<u16>(*abi);
    auto last_good = in.optional_bool("last_good");
    if (!last_good) return std::unexpected(std::move(last_good.error()));
    entry.last_good = *last_good;

    auto files = in.optional_array("files");
    if (!files) return std::unexpected(std::move(files.error()));
    for (std::size_t i = 0; i < (*files)->size(); ++i) {
        auto file_in = Reader::of((**files)[i], index_path(in.at("files"), i));
        if (!file_in) return std::unexpected(std::move(file_in.error()));
        auto role = file_in->string_at("role");
        if (!role) return std::unexpected(std::move(role.error()));
        const auto known = json_fields::find_named(json_fields::kRoleNames, *role);
        if (!known) return std::unexpected(malformed(file_in->at("role")));
        auto remote = json_fields::read_remote_file(*file_in);
        if (!remote) return std::unexpected(std::move(remote.error()));
        entry.size_bytes += remote->size;
        entry.files.push_back(PayloadFile{*known, std::move(*remote)});
    }
    return entry;
}

[[nodiscard]] Result<StoreEntry> read_runtime(const Reader& in) {
    StoreEntry entry;
    auto id = in.nonempty_string_at("id");
    if (!id) return std::unexpected(std::move(id.error()));
    auto version = in.nonempty_string_at("version");
    if (!version) return std::unexpected(std::move(version.error()));
    entry.ref = ComponentRef{ComponentKind::Runtime, std::move(*id), std::move(*version)};
    auto kind = in.string_at("kind");
    if (!kind) return std::unexpected(std::move(kind.error()));
    const auto known = json_fields::find_named(json_fields::kRuntimeKindNames, *kind);
    if (!known) return std::unexpected(malformed(in.at("kind")));
    entry.runtime_kind = *known;
    auto archive_in = sub_object(in, "archive");
    if (!archive_in) return std::unexpected(std::move(archive_in.error()));
    auto archive = json_fields::read_remote_file(*archive_in);
    if (!archive) return std::unexpected(std::move(archive.error()));
    entry.archive = std::move(*archive);
    auto unpacked = in.u64_at("unpacked_bytes");
    if (!unpacked) return std::unexpected(std::move(unpacked.error()));
    entry.size_bytes = *unpacked;
    auto last_good = in.optional_bool("last_good");
    if (!last_good) return std::unexpected(std::move(last_good.error()));
    entry.last_good = *last_good;
    return entry;
}

}  // namespace

Result<std::vector<StoreEntry>> parse_store_index(std::span<const u8> bytes) {
    boost::system::error_code error;
    const json::value document =
        json::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), error);
    if (error) return std::unexpected(malformed("index"));
    auto root = Reader::of(document, "");
    if (!root) return std::unexpected(std::move(root.error()));
    auto schema = root->u64_at("schema");
    if (!schema) return std::unexpected(std::move(schema.error()));
    if (*schema != kIndexSchema) {
        Diagnostic refused = malformed("schema");
        if (*schema > kIndexSchema) refused.kind = ErrorKind::Unsupported;
        return std::unexpected(std::move(refused));
    }

    std::vector<StoreEntry> entries;
    for (const auto& [key, read] : {std::pair{std::string_view("payloads"), &read_payload},
                                    std::pair{std::string_view("runtimes"), &read_runtime}}) {
        auto list = root->optional_array(key);
        if (!list) return std::unexpected(std::move(list.error()));
        for (std::size_t i = 0; i < (*list)->size(); ++i) {
            auto in = Reader::of((**list)[i], index_path(key, i));
            if (!in) return std::unexpected(std::move(in.error()));
            auto entry = read(*in);
            if (!entry) return std::unexpected(std::move(entry.error()));
            for (const StoreEntry& other : entries)
                if (other.ref == entry->ref) return std::unexpected(malformed(in->path()));
            entries.push_back(std::move(*entry));
        }
    }
    return entries;
}

std::vector<u8> serialize_store_index(const std::vector<StoreEntry>& entries) {
    json::array payloads;
    json::array runtimes;
    for (const StoreEntry& entry : entries) {
        json::object out;
        if (entry.ref.kind == ComponentKind::Payload) {
            out["version"] = entry.ref.version;
            out["payload_abi"] = entry.payload_abi;
            json::array files;
            for (const PayloadFile& file : entry.files) {
                json::object file_out;
                file_out["role"] = json_fields::name_of(json_fields::kRoleNames, file.role);
                json_fields::write_remote_file(file_out, file.file);
                files.emplace_back(std::move(file_out));
            }
            out["files"] = std::move(files);
            out["last_good"] = entry.last_good;
            payloads.emplace_back(std::move(out));
        } else {
            out["id"] = entry.ref.id;
            out["version"] = entry.ref.version;
            out["kind"] = json_fields::name_of(json_fields::kRuntimeKindNames, entry.runtime_kind);
            json::object archive;
            json_fields::write_remote_file(archive, entry.archive);
            out["archive"] = std::move(archive);
            out["unpacked_bytes"] = entry.size_bytes;
            out["last_good"] = entry.last_good;
            runtimes.emplace_back(std::move(out));
        }
    }
    json::object root;
    root["schema"] = kIndexSchema;
    root["payloads"] = std::move(payloads);
    root["runtimes"] = std::move(runtimes);
    const std::string text = json::serialize(root);
    return {text.begin(), text.end()};
}

}  // namespace reboot::components
