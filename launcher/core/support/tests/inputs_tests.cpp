#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "reboot/support/host_inputs.hpp"
#include "reboot/support/parse_matrix_report.hpp"
#include "reboot/support/version_range.hpp"

using namespace rb;
using namespace rb::support;

namespace {

std::span<const u8> bytes(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

contracts::game_server::GameServerDescription description(std::string min, std::string max,
                                                          std::vector<contracts::game_server::ClRange> cls = {}) {
    contracts::game_server::GameServerDescription out;
    out.supports.push_back({.version_min = std::move(min), .version_max = std::move(max), .cl_ranges = std::move(cls)});
    return out;
}

constexpr std::string_view kSha1 = "0101010101010101010101010101010101010101010101010101010101010101";

std::string report(std::string rows) { return R"({"schema":1,"cells":[)" + rows + "]}"; }

std::string play_row(std::string_view status, std::string_view runtime, std::string_view version = "8.51") {
    return std::string(R"({"role":"play","os":"linux","runtime":)") + std::string(runtime) +
           R"(,"range":{"min":"8.0","max":"8.51"},"build":"8.51","version":")" + std::string(version) +
           R"(","cl":6639283,"inputs":{"client_dll_sha256":")" + std::string(kSha1) +
           R"(","backend_content":{"schema":1,"serial":7}},"recorded_at":1790000000,"status":")" +
           std::string(status) + R"(","log_ref":"runs/1.log"})";
}

constexpr std::string_view kProton = R"({"id":"ge-proton-10","kind":"proton-umu"})";

}  // namespace

TEST_CASE("a range max without a patch covers every patch of its major.minor") {
    const VersionRange range{.min = {8, 0, std::nullopt}, .max = {10, 40, std::nullopt}, .changelists = {}};
    CHECK(range.contains({10, 40, u16{3}}, std::nullopt));
    CHECK(range.contains({8, 0, std::nullopt}, std::nullopt));
    CHECK_FALSE(range.contains({10, 41, std::nullopt}, std::nullopt));
    CHECK_FALSE(range.contains({7, 40, std::nullopt}, std::nullopt));
}

TEST_CASE("changelist ranges need a known changelist") {
    const VersionRange range{.min = {8, 0, std::nullopt},
                             .max = {8, 51, std::nullopt},
                             .changelists = {{.first = {100}, .last = {200}}}};
    CHECK(range.contains({8, 51, std::nullopt}, Changelist{150}));
    CHECK_FALSE(range.contains({8, 51, std::nullopt}, Changelist{201}));
    CHECK_FALSE(range.contains({8, 51, std::nullopt}, std::nullopt));
}

TEST_CASE("host_inputs_from keeps the binary's ranges and rejects malformed ones") {
    const components::Sha256Digest sha{};
    const Result<HostInputs> inputs = host_inputs_from(sha, description("3.5", "10.40", {{.first = 1, .last = 2}}));
    REQUIRE(inputs);
    REQUIRE(inputs->ranges.size() == 1);
    CHECK(inputs->ranges[0].max == GameVersion{10, 40, std::nullopt});
    CHECK(inputs->ranges[0].changelists.size() == 1);

    CHECK(host_inputs_from(sha, description("3.5", "10.40-CL-1")).error().is(msg::kMalformedServerDescription));
    CHECK(host_inputs_from(sha, description("10.40", "3.5")).error().is(msg::kMalformedServerDescription));
    CHECK(host_inputs_from(sha, description("3.5", "10.40", {{.first = 2, .last = 1}}))
              .error()
              .is(msg::kMalformedServerDescription));
}

TEST_CASE("the matrix report yields a record per pass or fail row") {
    const std::string body = report(play_row("pass", kProton) + "," + play_row("fail", kProton) + "," +
                                    play_row("untested", kProton) + "," +
                                    R"({"status":"blocked","blocked_reason":"runtime"})");
    const Result<std::vector<EvidenceRecord>> records = parse_matrix_report(bytes(body));
    REQUIRE(records);
    REQUIRE(records->size() == 2);
    const EvidenceRecord& first = records->front();
    CHECK(first.result == EvidenceResult::Pass);
    CHECK(records->back().result == EvidenceResult::Fail);
    CHECK(first.os == components::ManifestOs::Linux);
    CHECK(first.cell.role == SupportRole::Play);
    CHECK(first.cell.runner == ports::RunnerKind::Umu);
    CHECK(first.cl == Changelist{6639283});
    const auto* inputs = std::get_if<PlayCellInputs>(&first.inputs);
    REQUIRE(inputs);
    CHECK(inputs->runner_pin == "ge-proton-10");
    CHECK(inputs->backend_content.serial == 7);
    CHECK(inputs->client_dll_sha256[0] == 1);
}

TEST_CASE("the matrix report refuses what it cannot key evidence to") {
    CHECK(parse_matrix_report(bytes(R"({"schema":2,"cells":[]})")).error().is(msg::kMatrixReportUnknownSchema));
    CHECK(parse_matrix_report(bytes("not json")).error().is(msg::kMatrixReportMalformed));

    // Native has no runtime component to pin.
    CHECK(parse_matrix_report(bytes(report(play_row("pass", R"({"id":"x","kind":"native"})"))))
              .error()
              .is(msg::kMatrixReportMalformed));
    CHECK(parse_matrix_report(bytes(report(play_row("pass", R"({"kind":"dxvk"})")))).error().is(
        msg::kMatrixReportMalformed));
    CHECK(parse_matrix_report(bytes(report(play_row("pass", kProton, "9.10")))).error().is(
        msg::kMatrixReportMalformed));
    CHECK(parse_matrix_report(bytes(report(play_row("skipped", kProton)))).error().is(msg::kMatrixReportMalformed));

    const std::string host_on_wine =
        std::string(R"({"role":"host","os":"linux","runtime":{"id":"kron4ek-9","kind":"wine-kron4ek"},)") +
        R"("range":{"min":"8.0","max":"8.51"},"build":"8.51","version":"8.51","inputs":{"game_server_sha256":")" +
        std::string(kSha1) + R"("},"recorded_at":1,"status":"pass","log_ref":"h.log"})";
    CHECK(parse_matrix_report(bytes(report(host_on_wine))).error().is(msg::kMatrixReportMalformed));
}

TEST_CASE("a host row yields the game-server input on the Native runner") {
    const std::string host_row =
        std::string(R"({"role":"host","os":"windows","runtime":{"kind":"native"},)") +
        R"("range":{"min":"8.0","max":"8.51","changelists":[{"first":1,"last":9}]},"build":"8.51","version":"8.51",)" +
        R"("cl":5,"inputs":{"game_server_sha256":")" + std::string(kSha1) +
        R"("},"recorded_at":1790000000,"status":"pass","log_ref":"h.log"})";
    const Result<std::vector<EvidenceRecord>> records = parse_matrix_report(bytes(report(host_row)));
    REQUIRE(records);
    REQUIRE(records->size() == 1);
    const EvidenceRecord& record = records->front();
    CHECK(record.cell.role == SupportRole::Host);
    CHECK(record.cell.runner == ports::RunnerKind::Native);
    CHECK(record.cell.range.changelists == std::vector<ChangelistRange>{{.first = {1}, .last = {9}}});
    CHECK(record.os == components::ManifestOs::Windows);
    CHECK(record.recorded_at == std::chrono::system_clock::time_point(std::chrono::seconds(1790000000)));
    const auto* inputs = std::get_if<HostCellInputs>(&record.inputs);
    REQUIRE(inputs);
    CHECK(inputs->game_server_sha256[31] == 1);
}

TEST_CASE("a recorded_at the clock cannot represent is malformed") {
    std::string row = play_row("pass", kProton);
    const std::string_view at = R"("recorded_at":1790000000)";
    row.replace(row.find(at), at.size(), R"("recorded_at":18446744073709551615)");
    CHECK(parse_matrix_report(bytes(report(row))).error().is(msg::kMatrixReportMalformed));
}

TEST_CASE("a range max with a patch ends at that patch") {
    const VersionRange range{.min = {10, 40, u16{1}}, .max = {10, 40, u16{2}}, .changelists = {}};
    CHECK(range.contains({10, 40, u16{2}}, std::nullopt));
    CHECK_FALSE(range.contains({10, 40, u16{3}}, std::nullopt));
    CHECK_FALSE(range.contains({10, 40, u16{0}}, std::nullopt));
}

TEST_CASE("a server range from a patch to a patchless max of the same major.minor is not inverted") {
    const Result<HostInputs> inputs = host_inputs_from(components::Sha256Digest{}, description("10.40.3", "10.40"));
    REQUIRE(inputs);
    CHECK(inputs->ranges[0].contains({10, 40, u16{5}}, std::nullopt));
    CHECK_FALSE(inputs->ranges[0].contains({10, 40, u16{2}}, std::nullopt));
    CHECK(host_inputs_from(components::Sha256Digest{}, description("10.40.3", "10.40.2"))
              .error()
              .is(msg::kMalformedServerDescription));
}

TEST_CASE("a matrix-report digest must be lowercase hex") {
    std::string row = play_row("pass", kProton);
    std::string upper(kSha1);
    upper.replace(0, 2, "AB");
    row.replace(row.find(kSha1), kSha1.size(), upper);
    CHECK(parse_matrix_report(bytes(report(row))).error().is(msg::kMatrixReportMalformed));
}
