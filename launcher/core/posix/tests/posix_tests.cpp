#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <chrono>
#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/posix/peer_credential_check.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/process_start_time.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "unix_endpoint_checks.hpp"

using namespace reboot;
using namespace reboot::posix;

TEST_CASE("check_socket_path_fits counts the terminator against sun_path") {
    const NativePath fits_macos = "/" + std::string(102, 'a');
    const NativePath fits_linux_only = "/" + std::string(103, 'a');

    CHECK(check_socket_path_fits(fits_macos, 104).has_value());
    CHECK(check_socket_path_fits(fits_linux_only, 108).has_value());

    const auto too_long = check_socket_path_fits(fits_linux_only, 104);
    REQUIRE_FALSE(too_long.has_value());
    CHECK(too_long.error().is(kSocketPathTooLong));
    CHECK(too_long.error().kind == ErrorKind::InvalidInput);
    REQUIRE(too_long.error().find_arg("length") != nullptr);
    CHECK(*too_long.error().find_arg("length") == Arg{u64{105}});
}

TEST_CASE("octal_mode renders permission bits as four octal digits") {
    CHECK(octal_mode(0700) == "0700");
    CHECK(octal_mode(0755) == "0755");
    CHECK(octal_mode(040755) == "0755");
}

TEST_CASE("call_failed maps ENOENT to NotFound and keeps the errno") {
    const Diagnostic missing = call_failed("open", ENOENT, NativePath{"/missing"});
    CHECK(missing.is(kCallFailedOnPath));
    CHECK(missing.kind == ErrorKind::NotFound);
    CHECK(missing.os_error == SystemError{SystemError::Origin::Host, ENOENT});

    const Diagnostic denied = call_failed("open", EACCES);
    CHECK(denied.is(kCallFailed));
    CHECK(denied.kind == ErrorKind::Generic);
}

TEST_CASE("UniqueFd moves and releases ownership") {
    UniqueFd empty;
    CHECK_FALSE(empty.valid());

    UniqueFd first{1000};
    UniqueFd second{std::move(first)};
    CHECK_FALSE(first.valid());
    CHECK(second.get() == 1000);

    UniqueFd third;
    third = std::move(second);
    CHECK_FALSE(second.valid());
    CHECK(third.release() == 1000);
    CHECK_FALSE(third.valid());
}

TEST_CASE("same_start_time tolerates btime rounding but not another process") {
    const std::chrono::system_clock::time_point recorded{std::chrono::seconds{1'700'000'000}};

    CHECK(same_start_time(recorded, recorded));
    CHECK(same_start_time(recorded, recorded + kStartTimeTolerance));
    CHECK(same_start_time(recorded, recorded - std::chrono::seconds{1}));
    CHECK_FALSE(same_start_time(recorded, recorded + kStartTimeTolerance + std::chrono::milliseconds{1}));
    CHECK_FALSE(same_start_time(recorded, recorded - std::chrono::minutes{1}));
}

TEST_CASE("PeerCredentialCheck accepts only the expected uid") {
    PeerCredentials peer{.uid = 501, .pid = 42};
    int reads = 0;
    PeerCredentialCheck check{[&](int) -> Result<PeerCredentials> {
                                  ++reads;
                                  return peer;
                              },
                              501};

    const auto same_user = check.verify(7);
    REQUIRE(same_user.has_value());
    CHECK(same_user->user_id == "501");
    CHECK(same_user->pid == 42);

    peer.uid = 0;
    const auto other_user = check.verify(7);
    REQUIRE_FALSE(other_user.has_value());
    CHECK(other_user.error().is(kEndpointUntrusted));
    REQUIRE(other_user.error().causes.size() == 1);
    CHECK(other_user.error().causes.front().is(kPeerOtherUser));
    CHECK(reads == 2);
}

TEST_CASE("PeerCredentialCheck treats an unreadable peer as untrusted") {
    PeerCredentialCheck check{
        [](int) -> Result<PeerCredentials> { return std::unexpected(call_failed("getpeereid", EBADF)); }, 501};

    const auto result = check.verify(7);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().is(kEndpointUntrusted));
    REQUIRE(result.error().causes.size() == 1);
    CHECK(result.error().causes.front().is(kCallFailed));
}
