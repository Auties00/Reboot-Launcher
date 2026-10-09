#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "reboot/ipc/endpoint.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ports/ipc.hpp"

using namespace reboot;

namespace {

constexpr std::string_view kHash = "0123456789abcdef";
// Stands for a user whose per-user directory the OS module cannot name.
constexpr std::string_view kUnnamedUser = "4242";

}  // namespace

// The OS ipc package defines this in the real build; the test binary links none.
std::string reboot::ports::endpoint_name(const PeerIdentity& self, std::string_view root_hash16) {
    if (self.user_id == kUnnamedUser) return {};
    return "engine-" + self.user_id + "-" + std::string(root_hash16);
}

TEST_CASE("root hashes are exactly 16 lowercase hex digits", "[ipc][endpoint]") {
    CHECK(ipc::is_root_hash16(kHash));
    CHECK_FALSE(ipc::is_root_hash16("0123456789ABCDEF"));
    CHECK_FALSE(ipc::is_root_hash16("0123456789abcde"));
    CHECK_FALSE(ipc::is_root_hash16("0123456789abcdef0"));
    CHECK_FALSE(ipc::is_root_hash16("0123456789abcdeg"));
    CHECK_FALSE(ipc::is_root_hash16(""));
}

TEST_CASE("endpoint_for names the endpoint of a uid or a SID", "[ipc][endpoint]") {
    auto posix = ipc::endpoint_for(ports::PeerIdentity{"1000", 1}, kHash);
    REQUIRE(posix);
    CHECK(*posix == "engine-1000-0123456789abcdef");
    auto windows = ipc::endpoint_for(ports::PeerIdentity{"S-1-5-21-1004336348-1177238915-682003330-1001", 1}, kHash);
    REQUIRE(windows);
    CHECK(windows->starts_with("engine-S-1-5-21-"));
}

TEST_CASE("endpoint_for refuses input that could escape the endpoint name", "[ipc][endpoint]") {
    for (const std::string_view user : {"", "..", "1000/../0", "S-1-", "S-1-5-", "S-1-5--1", "s-1-5", "1000 ", "x"}) {
        auto result = ipc::endpoint_for(ports::PeerIdentity{std::string(user), 1}, kHash);
        REQUIRE_FALSE(result);
        CHECK(result.error().is(ipc::kInvalidEndpointInput));
        CHECK(result.error().kind == ErrorKind::InvalidInput);
    }
    auto hash = ipc::endpoint_for(ports::PeerIdentity{"1000", 1}, "../../etc/passwd");
    REQUIRE_FALSE(hash);
    CHECK(hash.error().is(ipc::kInvalidEndpointInput));
    CHECK(std::get<std::string>(*hash.error().find_arg("field")) == "root_hash16");
}

TEST_CASE("an endpoint the OS module cannot name is invalid input", "[ipc][endpoint]") {
    auto result = ipc::endpoint_for(ports::PeerIdentity{std::string(kUnnamedUser), 1}, kHash);
    REQUIRE_FALSE(result);
    CHECK(std::get<std::string>(*result.error().find_arg("field")) == "user_id");
}
