#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "engine_socket_path.hpp"
#include "messages.hpp"

using reboot::ArgSpec;
using reboot::Diagnostic;
using reboot::MessageSpec;
using reboot::NativePath;
using reboot::Result;
using reboot::os_macos::ipc::check_engine_socket_path;
using reboot::os_macos::ipc::engine_socket_path;

namespace {

const NativePath kUserTemp{"/var/folders/ab/cd/T"};
constexpr std::string_view kHash = "0123456789abcdef";
constexpr std::string_view kExpected = "/var/folders/ab/cd/T/reboot-launcher/0123456789abcdef.sock";

const MessageSpec* find_spec(std::string_view id) {
    for (const MessageSpec* spec : reboot::message_registry())
        if (spec->id == id) return spec;
    return nullptr;
}

void check_rejected(const NativePath& user_temp_dir, std::string_view endpoint_name) {
    INFO(endpoint_name);
    const Result<NativePath> result = check_engine_socket_path(user_temp_dir, endpoint_name);
    REQUIRE_FALSE(result);
    CHECK(result.error().is(reboot::posix::kEndpointUntrusted));
    REQUIRE(result.error().causes.size() == 1);
    CHECK(result.error().causes.front().is(reboot::os_macos::ipc::kEndpointOutsideUserTemp));
}

}  // namespace

TEST_CASE("the socket sits in reboot-launcher under the user temp dir", "[engine_socket_path]") {
    CHECK(engine_socket_path(kUserTemp, kHash).string() == kExpected);
    // confstr reports the directory with a trailing slash.
    CHECK(engine_socket_path(NativePath{"/var/folders/ab/cd/T/"}, kHash).string() == kExpected);
    CHECK(engine_socket_path(NativePath{}, kHash).empty());
}

TEST_CASE("exactly the expected socket path is accepted", "[engine_socket_path]") {
    const Result<NativePath> result = check_engine_socket_path(kUserTemp, kExpected);
    REQUIRE(result);
    CHECK(result->string() == kExpected);
}

TEST_CASE("any other socket path is untrusted", "[engine_socket_path]") {
    check_rejected(kUserTemp, "");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/0123456789abcdef");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/0123456789abcdef.socket");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/other/0123456789abcdef.sock");
    check_rejected(kUserTemp, "/tmp/reboot-launcher/0123456789abcdef.sock");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/../reboot-launcher/0123456789abcdef.sock");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/sub/0123456789abcdef.sock");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/..sock");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/.sock");
    check_rejected(kUserTemp, "/var/folders/ab/cd/T/reboot-launcher/0123456789ABCDEF.sock");
    check_rejected(kUserTemp, "reboot-launcher/0123456789abcdef.sock");
}

TEST_CASE("an unreadable user temp dir rejects every path", "[engine_socket_path]") {
    check_rejected(NativePath{}, "");
    check_rejected(NativePath{}, "/reboot-launcher/0123456789abcdef.sock");
    check_rejected(NativePath{}, "reboot-launcher/0123456789abcdef.sock");
}

TEST_CASE("the untrusted cause names every placeholder of its message", "[engine_socket_path]") {
    const Result<NativePath> result = check_engine_socket_path(kUserTemp, "/tmp/x.sock");
    REQUIRE_FALSE(result);
    REQUIRE(result.error().causes.size() == 1);
    const Diagnostic& cause = result.error().causes.front();
    const MessageSpec* spec = find_spec(cause.id);
    REQUIRE(spec != nullptr);
    CHECK(cause.args.size() == spec->args.size());
    for (const ArgSpec& arg : spec->args) {
        INFO(arg.name);
        CHECK(cause.find_arg(arg.name) != nullptr);
    }
}
