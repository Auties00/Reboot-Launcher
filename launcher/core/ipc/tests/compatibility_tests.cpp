#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/ipc.hpp"
#include "reboot/ipc/compatibility.hpp"

using namespace reboot;
using reboot::ipc::Compatibility;

TEST_CASE("only the same build is fully compatible", "[ipc]") {
    STATIC_CHECK(ipc::compatibility_for("1.2.3+abc", "1.2.3+abc") == Compatibility::Full);
    STATIC_CHECK(ipc::compatibility_for("1.2.3+abc", "1.2.4+def") == Compatibility::BootstrapOnly);
    STATIC_CHECK(ipc::compatibility_for("1.2.3+abc", "") == Compatibility::BootstrapOnly);
}

TEST_CASE("another build is served only the bootstrap methods", "[ipc]") {
    for (const u32 method : contracts::ipc::kBootstrapMethodIds) {
        CHECK(ipc::allows_method(Compatibility::BootstrapOnly, method));
        CHECK(ipc::allows_method(Compatibility::Full, method));
    }
    constexpr u32 kNonBootstrap = contracts::ipc::method_id(2, 1);
    STATIC_CHECK(!ipc::allows_method(Compatibility::BootstrapOnly, kNonBootstrap));
    STATIC_CHECK(ipc::allows_method(Compatibility::Full, kNonBootstrap));
}
