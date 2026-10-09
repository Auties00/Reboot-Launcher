#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

#include "integration_test_support.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/integration/entry_error.hpp"
#include "reboot/integration/prerequisite_error.hpp"
#include "reboot/integration/purge_error.hpp"
#include "reboot/integration/shell_error.hpp"

using namespace rb;
using namespace rb::integration;

namespace {

[[nodiscard]] std::string text_arg(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    REQUIRE(arg != nullptr);
    const auto* text = std::get_if<std::string>(arg);
    REQUIRE(text != nullptr);
    return *text;
}

[[nodiscard]] u64 count_arg(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    REQUIRE(arg != nullptr);
    const auto* count = std::get_if<u64>(arg);
    REQUIRE(count != nullptr);
    return *count;
}

}  // namespace

TEST_CASE("entry errors name the item and keep the registrar's cause", "[integration][errors]") {
    const Diagnostic foreign = to_diagnostic(
        EntryError{.code = EntryErrorCode::Foreign, .kind = IntegrationKind::UrlScheme, .owner = "other.exe"});
    CHECK(foreign.id == "integration.foreign_entry");
    CHECK(foreign.domain == ErrorDomain::Integration);
    CHECK(foreign.kind == ErrorKind::Conflict);
    CHECK(text_arg(foreign, "item") == "url_scheme");
    CHECK(text_arg(foreign, "owner") == "other.exe");

    const Diagnostic write = to_diagnostic(EntryError{.code = EntryErrorCode::WriteFailed,
                                                      .kind = IntegrationKind::EngineAgent,
                                                      .cause = test::fault("platform.denied")});
    CHECK(write.id == "integration.write_failed");
    CHECK(text_arg(write, "item") == "engine_agent");
    REQUIRE(write.causes.size() == 1);
    CHECK(write.causes[0].id == "platform.denied");

    CHECK(to_diagnostic(EntryError{.code = EntryErrorCode::NoItems}).kind == ErrorKind::InvalidInput);
    CHECK(to_diagnostic(EntryError{.code = EntryErrorCode::Unsupported, .kind = IntegrationKind::DesktopEntry}).kind ==
          ErrorKind::Unsupported);
    CHECK(to_diagnostic(EntryError{.code = EntryErrorCode::NotApplied}).id == "integration.not_applied");
}

TEST_CASE("prerequisite errors carry the stable id, or the unknown text", "[integration][errors]") {
    const Diagnostic unknown = to_diagnostic(PrerequisiteError{.code = PrerequisiteErrorCode::UnknownId, .text = "x.y"});
    CHECK(unknown.id == "integration.unknown_prerequisite");
    CHECK(text_arg(unknown, "id") == "x.y");

    const Diagnostic failed = to_diagnostic(PrerequisiteError{.code = PrerequisiteErrorCode::RemediationFailed,
                                                              .id = PrerequisiteId::MacRosetta,
                                                              .cause = test::fault()});
    CHECK(failed.id == "integration.remediation_failed");
    CHECK(text_arg(failed, "id") == "mac.rosetta");
    CHECK(failed.causes.size() == 1);

    CHECK(to_diagnostic(PrerequisiteError{.code = PrerequisiteErrorCode::NotRemediable, .id = PrerequisiteId::LinuxVulkan})
              .kind == ErrorKind::Unsupported);
    CHECK(to_diagnostic(PrerequisiteError{.code = PrerequisiteErrorCode::StillMissing, .id = PrerequisiteId::LinuxLinger})
              .id == "integration.prerequisite_still_missing");
}

TEST_CASE("purge errors count the blockers and list every failed target", "[integration][errors]") {
    const Diagnostic blocked =
        to_diagnostic(PurgeError{.code = PurgeErrorCode::Blocked, .sessions = 2, .ops = 1, .backend_running = true});
    CHECK(blocked.id == "integration.purge_blocked");
    CHECK(blocked.kind == ErrorKind::Conflict);
    CHECK(count_arg(blocked, "sessions") == 2);
    CHECK(count_arg(blocked, "operations") == 1);

    const Diagnostic failed = to_diagnostic(PurgeError{.code = PurgeErrorCode::RemoveFailed,
                                                       .path = NativePath("/data/logs"),
                                                       .causes = {test::fault("a"), test::fault("b")}});
    CHECK(failed.id == "integration.purge_failed");
    CHECK(failed.find_arg("path") != nullptr);
    CHECK(failed.causes.size() == 2);

    CHECK(to_diagnostic(PurgeError{.code = PurgeErrorCode::UnsafeTarget, .path = NativePath("/")}).id ==
          "integration.purge_unsafe_target");
}

TEST_CASE("shell errors name the sessions, the link or the path", "[integration][errors]") {
    const Diagnostic session = to_diagnostic(ShellError{.code = ShellErrorCode::EngineInOtherSession,
                                                        .caller_session = "2",
                                                        .engine_session = "1"});
    CHECK(session.id == "integration.engine_in_other_session");
    CHECK(text_arg(session, "caller_session") == "2");
    CHECK(text_arg(session, "engine_session") == "1");

    const Diagnostic url = to_diagnostic(ShellError{.code = ShellErrorCode::ShellFailed,
                                                    .action = ShellAction::OpenUrl,
                                                    .url = "https://example.com",
                                                    .cause = test::fault()});
    CHECK(url.id == "integration.shell_failed");
    CHECK(text_arg(url, "target") == "https://example.com");
    CHECK(url.causes.size() == 1);

    const Diagnostic path = to_diagnostic(
        ShellError{.code = ShellErrorCode::Cancelled, .action = ShellAction::Reveal, .path = NativePath("/x")});
    CHECK(path.id == "integration.shell_cancelled");
    CHECK(path.kind == ErrorKind::Cancelled);
    CHECK(std::holds_alternative<WirePath>(*path.find_arg("target")));

    CHECK(to_diagnostic(ShellError{.code = ShellErrorCode::NotHttps}).kind == ErrorKind::InvalidInput);
    CHECK(to_diagnostic(ShellError{.code = ShellErrorCode::NoDisplay}).id == "integration.no_display");
}
