#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include <boost/json/parse.hpp>

#include "reboot/compat/compat_document.hpp"
#include "reboot/storage/load_report.hpp"

using namespace rb;
using namespace rb::compat;

namespace {

CompatDocument read(std::string_view text, std::vector<storage::ValueIssue>& issues) {
    return CompatDocument::read(boost::json::parse(text).as_object(), issues);
}

}  // namespace

TEST_CASE("the compat document round-trips its records and unknown members", "[compat][document]") {
    CompatDocument document;
    document.prefixes.push_back(PrefixRecord{RunnerKind::Umu, RuntimeId{"GE-Proton11-7"}, "11-7", true});
    document.prefixes.push_back(PrefixRecord{RunnerKind::MacRuntime, RuntimeId{"mac-wine-11"}, "11.0", false});
    document.runtimes.push_back(RuntimeRecord{RuntimeId{"mac-wine-11"}, true, std::nullopt});
    document.runtimes.push_back(RuntimeRecord{
        RuntimeId{"umu-1.4.4"}, false,
        SlrInstall{"steamrt3 0.20250210.116596", std::chrono::system_clock::time_point{std::chrono::seconds{1'700'000'000}}}});
    document.unknown.emplace("future", 7);

    std::vector<storage::ValueIssue> issues;
    const CompatDocument back = CompatDocument::read(document.write(), issues);
    CHECK(issues.empty());
    CHECK(back.prefixes == document.prefixes);
    CHECK(back.runtimes == document.runtimes);
    CHECK(back.unknown == document.unknown);
}

TEST_CASE("a bad record is dropped with an issue and the rest are kept", "[compat][document]") {
    std::vector<storage::ValueIssue> issues;
    const CompatDocument document = read(R"({
        "prefixes": [
            {"runner": "wine", "runtime": "kron-11", "runtime_version": "11.0"},
            {"runner": "native", "runtime": "x", "runtime_version": "1"},
            {"runner": "umu", "runtime_version": "1"},
            {"runner": "wine", "runtime": "kron-12", "runtime_version": "12.0"},
            {"runner": "plan9", "runtime": "x", "runtime_version": "1"},
            "text"
        ],
        "runtimes": [
            {"runtime": "kron-11", "completed_session": true},
            {"runtime": "umu-1", "slr": {"build": "steamrt3 1"}},
            {"runtime": "kron-11"}
        ]
    })",
                                         issues);
    REQUIRE(document.prefixes.size() == 1);
    CHECK(document.prefixes[0] == PrefixRecord{RunnerKind::Wine, RuntimeId{"kron-11"}, "11.0", false});
    REQUIRE(document.runtimes.size() == 1);
    CHECK(document.runtimes[0].completed_session);

    std::vector<std::string> paths;
    for (const storage::ValueIssue& issue : issues) paths.push_back(issue.path);
    CHECK(paths == std::vector<std::string>{"prefixes[1]", "prefixes[2]", "prefixes[3]", "prefixes[4]", "prefixes[5]",
                                            "runtimes[1]", "runtimes[2]"});
    CHECK(issues[2].reason.id == "compat.record_duplicate");
    CHECK(issues[6].reason.id == "compat.record_duplicate");
}

TEST_CASE("a member that is not a list reads as empty", "[compat][document]") {
    std::vector<storage::ValueIssue> issues;
    const CompatDocument document = read(R"({"prefixes": {"runner": "wine"}})", issues);
    CHECK(document.prefixes.empty());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].reason.id == "compat.records_not_list");
}

TEST_CASE("schema 1 needs no upgrade", "[compat][document]") {
    const boost::json::object values{{"prefixes", boost::json::array{}}};
    const auto upgraded = CompatDocument::upgrade(values, 1);
    REQUIRE(upgraded);
    CHECK(*upgraded == values);
}
