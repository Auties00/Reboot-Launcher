#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "messages.hpp"
#include "reboot/engine/engine_command_line.hpp"
#include "reboot/engine/engine_origin.hpp"
#include "reboot/updates/resume_record.hpp"

using namespace rb;
using namespace rb::engine;

namespace {

Result<EngineCommandLine> parse(const std::vector<std::string>& args) {
    const std::vector<std::string_view> views(args.begin(), args.end());
    return parse_command_line(views);
}

void check_refused(const std::vector<std::string>& args, std::string_view argument) {
    const Result<EngineCommandLine> parsed = parse(args);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().is(msg::kBadCommandLine));
    const Arg* refused = parsed.error().find_arg("argument");
    REQUIRE(refused != nullptr);
    CHECK(std::get<std::string>(*refused) == argument);
}

}  // namespace

TEST_CASE("the updater's restart arguments parse back to the same origin") {
    for (const EngineOrigin origin : {EngineOrigin::OnDemand, EngineOrigin::ServiceManager, EngineOrigin::Foreground}) {
        const Result<EngineCommandLine> parsed = parse(updates::resume_args(origin));
        REQUIRE(parsed);
        CHECK(parsed->origin == origin);
        CHECK(parsed->resume);
    }
}

TEST_CASE("a bare run is an on-demand start") {
    const Result<EngineCommandLine> parsed = parse({"run"});
    REQUIRE(parsed);
    CHECK(parsed->origin == EngineOrigin::OnDemand);
    CHECK_FALSE(parsed->resume);
}

TEST_CASE("the command line refuses what it does not know") {
    check_refused({}, "");
    check_refused({"start"}, "start");
    check_refused({"run", "--origin=foreground"}, "--origin=foreground");
    check_refused({"run", "--foreground", "--origin=on-demand"}, "--origin=on-demand");
    check_refused({"run", "--origin=service-manager", "--foreground"}, "--foreground");
    check_refused({"run", "--resume", "--resume"}, "--resume");
}

TEST_CASE("a client-started successor of a resident engine stays resident") {
    CHECK(resumed_origin(EngineOrigin::OnDemand, EngineOrigin::ServiceManager) == EngineOrigin::ServiceManager);
    CHECK(resumed_origin(EngineOrigin::OnDemand, std::nullopt) == EngineOrigin::OnDemand);
    CHECK(resumed_origin(EngineOrigin::ServiceManager, EngineOrigin::OnDemand) == EngineOrigin::ServiceManager);
    CHECK(resumed_origin(EngineOrigin::Foreground, EngineOrigin::ServiceManager) == EngineOrigin::Foreground);
}
