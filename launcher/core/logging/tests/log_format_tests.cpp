#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>

#include "reboot/logging/log_format.hpp"

using namespace reboot;
using namespace reboot::logging;
using namespace std::chrono;

namespace {

LogRecord record_with(std::string text) {
    LogRecord record;
    record.time = sys_days{year{2026} / 10 / 8} + hours{13} + minutes{5} + seconds{9} + milliseconds{42};
    record.level = LogLevel::Info;
    record.category = LogCategory::GameOutput;
    record.text = std::move(text);
    return record;
}

}  // namespace

TEST_CASE("a line has the UTC time, level, category, session and text", "[logging][format]") {
    CHECK(format_log_line(record_with("hello")) == "2026-10-08T13:05:09.042Z INFO  game_output hello\n");

    LogRecord with_session = record_with("hi");
    with_session.level = LogLevel::Error;
    with_session.session = SessionId{};
    with_session.session->value.bytes.fill(0x11);
    CHECK(format_log_line(with_session) ==
          "2026-10-08T13:05:09.042Z ERROR game_output 11111111-1111-1111-1111-111111111111 hi\n");
}

TEST_CASE("record text cannot forge a line of its own", "[logging][format]") {
    CHECK(format_log_line(record_with("a\n2026-10-08T00:00:00.000Z ERROR engine forged")) ==
          "2026-10-08T13:05:09.042Z INFO  game_output a\n    2026-10-08T00:00:00.000Z ERROR engine forged\n");
    CHECK(format_log_line(record_with("a\rb\x1b[2Jc\x7f\td")) ==
          "2026-10-08T13:05:09.042Z INFO  game_output a\\x0db\\x1b[2Jc\\x7f\td\n");
}

TEST_CASE("severities map to log levels", "[logging][format]") {
    CHECK(level_for(Severity::Info) == LogLevel::Info);
    CHECK(level_for(Severity::Warning) == LogLevel::Warn);
    CHECK(level_for(Severity::Error) == LogLevel::Error);
}

TEST_CASE("a diagnostic logs its id, args, detail, OS error and causes", "[logging][format]") {
    Diagnostic cause;
    cause.id = "posix.io";
    cause.os_error = SystemError{SystemError::Origin::GuestWindows, 5};

    Diagnostic diag;
    diag.id = "logging.write_failed";
    diag.args = {{"path", Arg{std::string("/logs/a.log")}}, {"count", Arg{u64{3}}}, {"retry", Arg{true}}};
    diag.detail = "disk full";
    diag.os_error = SystemError{SystemError::Origin::Host, 28};
    diag.causes.push_back(cause);

    CHECK(format_diagnostic(diag) ==
          "logging.write_failed(path=\"/logs/a.log\", count=3, retry=true) detail=\"disk full\" os=host:28"
          " <- posix.io() os=guest:5");
}
