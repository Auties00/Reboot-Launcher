#include "reboot/support/parse_matrix_report.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "messages.hpp"

namespace rb::support {
namespace {

namespace json = boost::json;

// The clock's tick is finer than a second, so larger values would overflow the time_point.
constexpr u64 kMaxRecordedAt = static_cast<u64>(
    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::duration::max()).count());

[[nodiscard]] std::unexpected<Diagnostic> malformed(std::string where) {
    return make_diag(ErrorDomain::Support, msg::kMatrixReportMalformed).arg("where", std::move(where)).fail();
}

[[nodiscard]] std::optional<std::string_view> string_at(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) return std::nullopt;
    return std::string_view(value->get_string());
}

[[nodiscard]] std::optional<u64> unsigned_at(const json::object& object, std::string_view key) {
    const json::value* value = object.if_contains(key);
    if (value == nullptr) return std::nullopt;
    if (value->is_uint64()) return value->get_uint64();
    if (value->is_int64() && value->get_int64() >= 0) return static_cast<u64>(value->get_int64());
    return std::nullopt;
}

[[nodiscard]] std::optional<u32> u32_at(const json::object& object, std::string_view key) {
    const std::optional<u64> value = unsigned_at(object, key);
    if (!value || *value > 0xFFFF'FFFFu) return std::nullopt;
    return static_cast<u32>(*value);
}

[[nodiscard]] std::optional<GameVersion> version_at(const json::object& object, std::string_view key) {
    const std::optional<std::string_view> text = string_at(object, key);
    if (!text) return std::nullopt;
    Result<GameVersion> version = GameVersion::parse(*text);
    if (!version) return std::nullopt;
    return *version;
}

[[nodiscard]] std::optional<u8> hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
    return std::nullopt;
}

// Lowercase hex, as foundation's to_hex writes it.
[[nodiscard]] std::optional<components::Sha256Digest> digest_at(const json::object& object, std::string_view key) {
    const std::optional<std::string_view> text = string_at(object, key);
    components::Sha256Digest digest{};
    if (!text || text->size() != digest.size() * 2) return std::nullopt;
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const std::optional<u8> high = hex_digit((*text)[2 * i]);
        const std::optional<u8> low = hex_digit((*text)[2 * i + 1]);
        if (!high || !low) return std::nullopt;
        digest[i] = static_cast<u8>(*high << 4 | *low);
    }
    return digest;
}

[[nodiscard]] std::optional<components::ManifestOs> os_from(std::string_view text) noexcept {
    if (text == "windows") return components::ManifestOs::Windows;
    if (text == "macos") return components::ManifestOs::MacOs;
    if (text == "linux") return components::ManifestOs::Linux;
    return std::nullopt;
}

// The rig's runtime kinds, from testing-strategy.
[[nodiscard]] std::optional<ports::RunnerKind> runner_from(std::string_view kind) noexcept {
    if (kind == "native") return ports::RunnerKind::Native;
    if (kind == "proton-umu") return ports::RunnerKind::Umu;
    if (kind == "wine-kron4ek") return ports::RunnerKind::Wine;
    if (kind == "gcenx" || kind == "crossover-src-dxmt") return ports::RunnerKind::MacRuntime;
    return std::nullopt;
}

[[nodiscard]] Result<VersionRange> parse_range(const json::object& row, const std::string& where) {
    const json::value* value = row.if_contains("range");
    if (value == nullptr || !value->is_object()) return malformed(where + ".range");
    const json::object& object = value->get_object();
    const std::optional<GameVersion> min = version_at(object, "min");
    if (!min) return malformed(where + ".range.min");
    const std::optional<GameVersion> max = version_at(object, "max");
    if (!max) return malformed(where + ".range.max");
    VersionRange range{.min = *min, .max = *max, .changelists = {}};

    if (const json::value* changelists = object.if_contains("changelists")) {
        if (!changelists->is_array()) return malformed(where + ".range.changelists");
        const json::array& array = changelists->get_array();
        for (std::size_t i = 0; i < array.size(); ++i) {
            const std::string at = where + ".range.changelists[" + std::to_string(i) + "]";
            if (!array[i].is_object()) return malformed(at);
            const std::optional<u32> first = u32_at(array[i].get_object(), "first");
            const std::optional<u32> last = u32_at(array[i].get_object(), "last");
            if (!first || !last || *first > *last) return malformed(at);
            range.changelists.push_back({.first = {*first}, .last = {*last}});
        }
    }
    return range;
}

[[nodiscard]] Result<CellInputs> parse_inputs(const json::object& row, SupportRole role, ports::RunnerKind runner,
                                              std::string runtime_id, const std::string& where) {
    const json::value* value = row.if_contains("inputs");
    if (value == nullptr || !value->is_object()) return malformed(where + ".inputs");
    const json::object& object = value->get_object();

    if (role == SupportRole::Host) {
        // Hosting runs our native game server only.
        if (runner != ports::RunnerKind::Native) return malformed(where + ".runtime.kind");
        const std::optional<components::Sha256Digest> server = digest_at(object, "game_server_sha256");
        if (!server) return malformed(where + ".inputs.game_server_sha256");
        return HostCellInputs{.game_server_sha256 = *server};
    }

    const std::optional<components::Sha256Digest> client = digest_at(object, "client_dll_sha256");
    if (!client) return malformed(where + ".inputs.client_dll_sha256");
    const json::value* content = object.if_contains("backend_content");
    if (content == nullptr || !content->is_object()) return malformed(where + ".inputs.backend_content");
    const std::optional<u32> schema = u32_at(content->get_object(), "schema");
    const std::optional<u64> serial = unsigned_at(content->get_object(), "serial");
    if (!schema || !serial) return malformed(where + ".inputs.backend_content");
    return PlayCellInputs{.client_dll_sha256 = *client,
                          .backend_content = {.schema = *schema, .serial = *serial},
                          .runner_pin = std::move(runtime_id)};
}

[[nodiscard]] Result<std::optional<EvidenceRecord>> parse_row(const json::value& value, const std::string& where) {
    if (!value.is_object()) return malformed(where);
    const json::object& row = value.get_object();

    const std::optional<std::string_view> status = string_at(row, "status");
    if (!status) return malformed(where + ".status");
    if (*status == "untested" || *status == "blocked") return std::nullopt;
    if (*status != "pass" && *status != "fail") return malformed(where + ".status");

    const std::optional<std::string_view> role_text = string_at(row, "role");
    if (!role_text || (*role_text != "play" && *role_text != "host")) return malformed(where + ".role");
    const SupportRole role = *role_text == "play" ? SupportRole::Play : SupportRole::Host;

    const std::optional<std::string_view> os_text = string_at(row, "os");
    const std::optional<components::ManifestOs> os = os_text ? os_from(*os_text) : std::nullopt;
    if (!os) return malformed(where + ".os");

    const json::value* runtime = row.if_contains("runtime");
    if (runtime == nullptr || !runtime->is_object()) return malformed(where + ".runtime");
    const std::optional<std::string_view> kind = string_at(runtime->get_object(), "kind");
    const std::optional<ports::RunnerKind> runner = kind ? runner_from(*kind) : std::nullopt;
    if (!runner) return malformed(where + ".runtime.kind");
    const std::optional<std::string_view> runtime_id = string_at(runtime->get_object(), "id");
    // Native has no runtime component, so its pin is empty.
    if ((*runner == ports::RunnerKind::Native) != (!runtime_id || runtime_id->empty()))
        return malformed(where + ".runtime.id");

    Result<VersionRange> range = parse_range(row, where);
    if (!range) return std::unexpected(std::move(range.error()));

    const std::optional<std::string_view> build = string_at(row, "build");
    if (!build || build->empty()) return malformed(where + ".build");
    const std::optional<GameVersion> version = version_at(row, "version");
    if (!version) return malformed(where + ".version");
    std::optional<Changelist> cl;
    if (row.contains("cl")) {
        const std::optional<u32> number = u32_at(row, "cl");
        if (!number) return malformed(where + ".cl");
        cl = Changelist{*number};
    }
    if (!range->contains(*version, cl)) return malformed(where + ".version");

    const std::optional<u64> recorded_at = unsigned_at(row, "recorded_at");
    if (!recorded_at || *recorded_at > kMaxRecordedAt) return malformed(where + ".recorded_at");
    const std::optional<std::string_view> log_ref = string_at(row, "log_ref");
    if (!log_ref) return malformed(where + ".log_ref");

    Result<CellInputs> inputs = parse_inputs(row, role, *runner, std::string(runtime_id.value_or("")), where);
    if (!inputs) return std::unexpected(std::move(inputs.error()));

    return EvidenceRecord{
        .cell = {.range = std::move(*range), .role = role, .runner = *runner},
        .inputs = std::move(*inputs),
        .os = *os,
        .build = CatalogEntryId(*build),
        .version = *version,
        .cl = cl,
        .recorded_at = std::chrono::system_clock::time_point(std::chrono::seconds(static_cast<i64>(*recorded_at))),
        .result = *status == "pass" ? EvidenceResult::Pass : EvidenceResult::Fail,
        .log_ref = std::string(*log_ref),
    };
}

}  // namespace

Result<std::vector<EvidenceRecord>> parse_matrix_report(std::span<const u8> json_body) {
    boost::system::error_code error;
    const json::value document =
        json::parse(std::string_view(reinterpret_cast<const char*>(json_body.data()), json_body.size()), error);
    if (error || !document.is_object()) return malformed("$");
    const json::object& root = document.get_object();

    const std::optional<u32> schema = u32_at(root, "schema");
    if (!schema) return malformed("schema");
    if (*schema != kMatrixReportSchema)
        return make_diag(ErrorDomain::Support, msg::kMatrixReportUnknownSchema).arg("schema", *schema).fail();

    const json::value* cells = root.if_contains("cells");
    if (cells == nullptr || !cells->is_array()) return malformed("cells");
    const json::array& rows = cells->get_array();

    std::vector<EvidenceRecord> records;
    records.reserve(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        Result<std::optional<EvidenceRecord>> record = parse_row(rows[i], "cells[" + std::to_string(i) + "]");
        if (!record) return std::unexpected(std::move(record.error()));
        if (*record) records.push_back(std::move(**record));
    }
    return records;
}

}  // namespace rb::support
