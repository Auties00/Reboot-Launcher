#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <vector>

#include "abi_boundary.hpp"
#include "messages.hpp"
#include "reboot/client.h"
#include "reboot/ipc/ipc_errors.hpp"

using namespace reboot;
using namespace reboot::client;

TEST_CASE("an output buffer is filled, then wiped and zeroed on release", "[client][abi]") {
    rb_buffer out{};
    REQUIRE(check_output(&out, "out"));
    fill_output(out, {1, 2, 3});
    REQUIRE(out.size == 3);
    CHECK(out.data[2] == 3);
    // A buffer that still holds data is refused.
    CHECK(check_output(&out, "out").error().is(msg::kInvalidArgument));
    CHECK_FALSE(check_output(nullptr, "out"));

    rb_buffer_release(&out);
    CHECK(out.data == nullptr);
    CHECK(out.size == 0);
    CHECK(out.internal == nullptr);
    rb_buffer_release(&out);
    rb_buffer_release(nullptr);
}

TEST_CASE("an empty output owns nothing", "[client][abi]") {
    rb_buffer out{};
    fill_output(out, {});
    CHECK(out.internal == nullptr);
    CHECK(check_output(&out, "out"));
}

TEST_CASE("a failure is recorded per thread and cleared by the next call", "[client][abi]") {
    CHECK(fail(make_diag(ErrorDomain::Ipc, ipc::kRootMismatch).build()) == RB_E_ENGINE_ROOT_MISMATCH);
    CHECK_FALSE(last_error().empty());

    rb_buffer diagnostic{};
    REQUIRE(rb_last_error(&diagnostic) == RB_OK);
    CHECK(diagnostic.size == last_error().size());
    rb_buffer_release(&diagnostic);

    CHECK(guarded("test", []() -> rb_status { return RB_OK; }) == RB_OK);
    CHECK(last_error().empty());
}

TEST_CASE("an exception inside an export becomes RB_E_INTERNAL with internal.bug", "[client][abi]") {
    const rb_status status = guarded("test", []() -> rb_status { throw std::runtime_error("boom"); });
    CHECK(status == RB_E_INTERNAL);
    CHECK_FALSE(last_error().empty());
    CHECK(rb_status_name(status) == std::string_view{"RB_E_INTERNAL"});
    CHECK(rb_status_name(12345) == std::string_view{"RB_UNKNOWN"});
}
