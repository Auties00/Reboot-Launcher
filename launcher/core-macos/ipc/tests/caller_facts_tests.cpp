#include <catch2/catch_test_macros.hpp>

#include "caller_facts.hpp"

using reboot::os_macos::ipc::caller_context_from;

TEST_CASE("the audit session id is the os session, in decimal", "[caller_facts]") {
    const auto context = caller_context_from({.audit_session_id = 100008, .graphic_access = true, .euid = 501});
    CHECK(context.os_session == "100008");
    CHECK(context.interactive);
    CHECK_FALSE(context.elevated);
    CHECK(context.display_env.empty());
}

TEST_CASE("a session without graphic access is not interactive", "[caller_facts]") {
    // An SSH login gets an audit session of its own without sessionHasGraphicAccess.
    const auto context = caller_context_from({.audit_session_id = 7, .graphic_access = false, .euid = 501});
    CHECK_FALSE(context.interactive);
    CHECK(context.os_session == "7");
}

TEST_CASE("only an effective uid of 0 is elevated", "[caller_facts]") {
    CHECK(caller_context_from({.audit_session_id = 1, .graphic_access = true, .euid = 0}).elevated);
    CHECK_FALSE(caller_context_from({.audit_session_id = 1, .graphic_access = true, .euid = 1}).elevated);
}
