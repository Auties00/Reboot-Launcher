#include <catch2/catch_test_macros.hpp>
#include <string>

#include "reboot/host/host_error.hpp"

using namespace rb;
using namespace rb::host;

namespace {

const MessageSpec* find_spec(const std::string& id) {
    for (const MessageSpec* spec : message_registry())
        if (spec->id == id) return spec;
    return nullptr;
}

}  // namespace

TEST_CASE("every host error names each placeholder of its message", "[host]") {
    for (u8 i = 0; i <= static_cast<u8>(HostErrorCode::ServerUnresponsive); ++i) {
        const Diagnostic diag = to_diagnostic(HostError{.code = static_cast<HostErrorCode>(i)});
        INFO(diag.id);
        CHECK(diag.domain == ErrorDomain::Host);
        const MessageSpec* spec = find_spec(diag.id);
        REQUIRE(spec != nullptr);
        CHECK(diag.args.size() == spec->args.size());
        for (const ArgSpec& arg : spec->args) {
            INFO(arg.name);
            CHECK(diag.find_arg(arg.name) != nullptr);
        }
    }
}

TEST_CASE("a cause travels with the host error", "[host]") {
    HostError error{.code = HostErrorCode::ListenTimeout};
    error.cause = make_diag(ErrorDomain::GameServer, MessageId{"gameserver.not_running"}).build();
    const Diagnostic diag = to_diagnostic(error);
    REQUIRE(diag.causes.size() == 1);
    CHECK(diag.causes[0].id == "gameserver.not_running");
}
