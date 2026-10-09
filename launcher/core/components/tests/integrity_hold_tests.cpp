#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <string>

#include "components_test_support.hpp"
#include "reboot/components/integrity_hold.hpp"
#include "reboot/foundation/sha256.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

using namespace rb;
using namespace rb::components;
using rb::components::test::arg_text;
using rb::components::test::bytes_of;
using rb::testing::FsOperation;
using rb::testing::InMemoryFileSystem;

namespace {

constexpr MessageId kPortError{"components_test.port_error"};

Diagnostic port_error(ErrorKind kind, std::optional<SystemError> os = std::nullopt) {
    Diagnostic diag = make_diag(ErrorDomain::Platform, kPortError).kind(kind).build();
    diag.os_error = os;
    return diag;
}

Diagnostic host_error(i64 code) { return port_error(ErrorKind::Generic, SystemError{SystemError::Origin::Host, code}); }

const NativePath kFile = NativePath("/components/payload/rb_client.dll");

}  // namespace

TEST_CASE("hold failures are classified from the port's diagnostic", "[components][hold]") {
    CHECK(classify_hold_failure(port_error(ErrorKind::NotFound)) == HoldFailureKind::Missing);
    CHECK(classify_hold_failure(port_error(ErrorKind::Conflict)) == HoldFailureKind::InUse);
    CHECK(classify_hold_failure(port_error(ErrorKind::Generic)) == HoldFailureKind::ReadFailed);
    // A guest code from winhost is always a Win32 code.
    const auto guest = [](i64 code) {
        return port_error(ErrorKind::Generic, SystemError{SystemError::Origin::GuestWindows, code});
    };
    CHECK(classify_hold_failure(guest(5)) == HoldFailureKind::AccessDenied);
    // ERROR_VIRUS_INFECTED and ERROR_VIRUS_DELETED ask the security probe like access denied.
    CHECK(classify_hold_failure(guest(225)) == HoldFailureKind::AccessDenied);
    CHECK(classify_hold_failure(guest(226)) == HoldFailureKind::AccessDenied);
    CHECK(classify_hold_failure(guest(32)) == HoldFailureKind::InUse);
    CHECK(classify_hold_failure(guest(33)) == HoldFailureKind::InUse);
    CHECK(classify_hold_failure(guest(2)) == HoldFailureKind::Missing);
    CHECK(classify_hold_failure(guest(1117)) == HoldFailureKind::ReadFailed);
#if defined(_WIN32)
    CHECK(classify_hold_failure(host_error(5)) == HoldFailureKind::AccessDenied);
    CHECK(classify_hold_failure(host_error(32)) == HoldFailureKind::InUse);
    CHECK(classify_hold_failure(host_error(3)) == HoldFailureKind::Missing);
#else
    CHECK(classify_hold_failure(host_error(EACCES)) == HoldFailureKind::AccessDenied);
    CHECK(classify_hold_failure(host_error(EPERM)) == HoldFailureKind::AccessDenied);
    CHECK(classify_hold_failure(host_error(ENOENT)) == HoldFailureKind::Missing);
    CHECK(classify_hold_failure(host_error(EIO)) == HoldFailureKind::ReadFailed);
#endif
}

TEST_CASE("each hold failure maps to its own message with the cause attached", "[components][hold]") {
    const auto diag = [](HoldFailureKind kind, std::optional<Diagnostic> cause) {
        return to_diagnostic(HoldFailure{kind, kFile, std::move(cause)});
    };
    const Diagnostic missing = diag(HoldFailureKind::Missing, port_error(ErrorKind::NotFound));
    CHECK(missing.id == "components.held_file_missing");
    CHECK(missing.kind == ErrorKind::NotFound);
    REQUIRE(missing.causes.size() == 1);
    CHECK(missing.causes[0].id == "components_test.port_error");
    CHECK(arg_text(missing, "file") == display_utf8(kFile));

    const Diagnostic in_use = diag(HoldFailureKind::InUse, port_error(ErrorKind::Conflict));
    CHECK(in_use.id == "components.file_in_use");
    CHECK(in_use.kind == ErrorKind::Conflict);
    CHECK(in_use.retryable);
    CHECK(diag(HoldFailureKind::AccessDenied, host_error(5)).id == "components.held_file_access_denied");
    CHECK(diag(HoldFailureKind::ReadFailed, port_error(ErrorKind::Generic)).id == "components.held_file_unreadable");
    const Diagnostic changed = diag(HoldFailureKind::Mismatch, std::nullopt);
    CHECK(changed.id == "components.held_file_changed");
    CHECK(changed.causes.empty());
}

TEST_CASE("acquire hashes through the held handle and keeps it open", "[components][hold]") {
    InMemoryFileSystem fs;
    fs.write_text(kFile, "client bytes");
    const Sha256Digest digest = sha256(bytes_of("client bytes"));

    auto held = IntegrityHold::acquire(fs, kFile, digest);
    REQUIRE(held);
    CHECK(held->held());
    CHECK(held->path() == kFile);
    CHECK(held->sha256() == digest);
    // The handle denies writers while it is held.
    CHECK_FALSE(fs.atomic_replace(kFile, bytes_of("swapped"), false));

    IntegrityHold moved = std::move(*held);
    CHECK_FALSE(held->held());
    CHECK(moved.held());
    moved.release();
    CHECK_FALSE(moved.held());
    CHECK(fs.atomic_replace(kFile, bytes_of("swapped"), false));
}

TEST_CASE("acquire reports a changed file as Mismatch and lets it go", "[components][hold]") {
    InMemoryFileSystem fs;
    fs.write_text(kFile, "tampered");
    const auto held = IntegrityHold::acquire(fs, kFile, sha256(bytes_of("client bytes")));
    REQUIRE_FALSE(held);
    CHECK(held.error().kind == HoldFailureKind::Mismatch);
    CHECK(held.error().file == kFile);
    CHECK_FALSE(held.error().cause);
    CHECK(fs.atomic_replace(kFile, bytes_of("restored"), false));
}

TEST_CASE("acquire classifies open failures", "[components][hold]") {
    InMemoryFileSystem fs;
    const auto missing = IntegrityHold::acquire(fs, kFile, Sha256Digest{});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().kind == HoldFailureKind::Missing);
    REQUIRE(missing.error().cause);

    fs.write_text(kFile, "x");
    fs.faults().fail_next(FsOperation::OpenDenyWrite, port_error(ErrorKind::Conflict));
    const auto in_use = IntegrityHold::acquire(fs, kFile, Sha256Digest{});
    REQUIRE_FALSE(in_use);
    CHECK(in_use.error().kind == HoldFailureKind::InUse);
}

TEST_CASE("acquire without a digest only learns it and never mismatches", "[components][hold]") {
    InMemoryFileSystem fs;
    fs.write_text(kFile, "custom auth dll");
    const auto held = IntegrityHold::acquire(fs, kFile);
    REQUIRE(held);
    CHECK(held->sha256() == sha256(bytes_of("custom auth dll")));
}
