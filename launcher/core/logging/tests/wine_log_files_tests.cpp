#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>
#include <vector>

#include "logging_test_support.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/logging/log_files_in_use.hpp"
#include "reboot/logging/wine_log_files.hpp"
#include "reboot/testing/in_memory_log_file_system.hpp"

using namespace rb;
using namespace rb::logging;
using rb::testing::InMemoryLogFileSystem;
using rb::testing::LogFsOperation;

namespace {

struct WineFixture {
    explicit WineFixture(u64 cap = kWineLogCapBytes) : files(clock), wine(options(cap), files, in_use) {}

    WineLogOptions options(u64 cap) {
        (void)files.create_directories(dir);
        return WineLogOptions{dir, test::kGroup, cap};
    }

    [[nodiscard]] NativePath path_of(const SessionId& session) const {
        return dir / wine_log_file_name(test::kGroup, session);
    }

    ManualClock clock;
    NativePath dir = NativePath("/data/logs").make_preferred();
    InMemoryLogFileSystem files;
    LogFilesInUse in_use;
    WineLogFiles wine;
};

}  // namespace

TEST_CASE("Wine routing takes only Wine lines that belong to a session", "[logging][wine]") {
    CHECK(routes_to_wine_log(test::record_of("x", LogCategory::Wine, test::session_of(1))));
    CHECK_FALSE(routes_to_wine_log(test::record_of("x", LogCategory::Wine)));
    CHECK_FALSE(routes_to_wine_log(test::record_of("x", LogCategory::GameOutput, test::session_of(1))));
}

TEST_CASE("a session's Wine log opens on its first line and stays in use while open", "[logging][wine]") {
    WineFixture f;
    const SessionId session = test::session_of(0xab);
    CHECK(f.files.file_names(f.dir).empty());

    REQUIRE(f.wine.append(session, "fixme:one\n"));
    REQUIRE(f.wine.append(session, "fixme:two\n"));
    CHECK(f.files.text(f.path_of(session)) == "fixme:one\nfixme:two\n");
    CHECK(f.in_use.snapshot() == std::vector{f.path_of(session)});
    CHECK(f.files.open_handles(f.path_of(session)) == 1);

    f.wine.close_session(session);
    CHECK(f.in_use.snapshot().empty());
    CHECK(f.files.open_handles(f.path_of(session)) == 0);

    // A later line reopens for append; nothing is truncated.
    REQUIRE(f.wine.append(session, "fixme:three\n"));
    CHECK(f.files.text(f.path_of(session)) == "fixme:one\nfixme:two\nfixme:three\n");
    CHECK(f.wine.status().failed_writes == 0);
}

TEST_CASE("each session has its own Wine log", "[logging][wine]") {
    WineFixture f;
    REQUIRE(f.wine.append(test::session_of(1), "a\n"));
    REQUIRE(f.wine.append(test::session_of(2), "b\n"));
    CHECK(f.files.text(f.path_of(test::session_of(1))) == "a\n");
    CHECK(f.files.text(f.path_of(test::session_of(2))) == "b\n");
    CHECK(f.in_use.snapshot().size() == 2);
    f.wine.close_session(test::session_of(1));
    CHECK(f.in_use.snapshot() == std::vector{f.path_of(test::session_of(2))});
}

TEST_CASE("past the cap one truncation line is written, then lines are dropped", "[logging][wine]") {
    WineFixture f(20);
    const SessionId session = test::session_of(3);
    REQUIRE(f.wine.append(session, "0123456789\n"));
    REQUIRE(f.wine.append(session, "0123456789\n"));
    REQUIRE(f.wine.append(session, "x\n"));
    CHECK(f.files.text(f.path_of(session)) == "0123456789\n[wine log reached its 20 byte cap; later lines are dropped]\n");
    CHECK(f.wine.status().dropped_lines == 2);

    // The cap holds across a reopen, without a second truncation line.
    f.wine.close_session(session);
    REQUIRE(f.wine.append(session, "y\n"));
    CHECK(f.files.text(f.path_of(session)) == "0123456789\n[wine log reached its 20 byte cap; later lines are dropped]\n");
    CHECK(f.wine.status().dropped_lines == 3);
}

TEST_CASE("a Wine log that fails to open is not retried until the session closes", "[logging][wine]") {
    WineFixture f;
    const SessionId session = test::session_of(4);
    f.files.faults().fail_next(LogFsOperation::OpenAppend, test::fault("platform.disk_full"));

    const Result<void> first = f.wine.append(session, "a\n");
    REQUIRE_FALSE(first);
    CHECK(first.error().id == "logging.open_failed");
    REQUIRE(first.error().causes.size() == 1);
    CHECK(first.error().causes[0].id == "platform.disk_full");

    // The fault is spent, so a retry would succeed; a failed session is not retried.
    const Result<void> second = f.wine.append(session, "b\n");
    REQUIRE_FALSE(second);
    CHECK(second.error().id == "logging.open_failed");
    const WineLogStatus status = f.wine.status();
    CHECK(status.failed_writes == 1);
    REQUIRE(status.last_failure);
    CHECK(status.last_failure->id == "logging.open_failed");
    CHECK_FALSE(f.files.exists(f.path_of(session)));
    CHECK(f.in_use.snapshot().empty());

    f.wine.close_session(session);
    REQUIRE(f.wine.append(session, "c\n"));
    CHECK(f.files.text(f.path_of(session)) == "c\n");
}

TEST_CASE("a failed Wine write closes the file and leaves the line to the caller", "[logging][wine]") {
    WineFixture f;
    const SessionId session = test::session_of(5);
    REQUIRE(f.wine.append(session, "a\n"));
    f.files.faults().fail_next(LogFsOperation::Append, test::fault());

    const Result<void> failed = f.wine.append(session, "b\n");
    REQUIRE_FALSE(failed);
    CHECK(failed.error().id == "logging.write_failed");
    CHECK(f.in_use.snapshot().empty());
    CHECK(f.files.open_handles(f.path_of(session)) == 0);
    CHECK(f.wine.status().failed_writes == 1);

    // Another session is unaffected.
    REQUIRE(f.wine.append(test::session_of(6), "other\n"));
    CHECK(f.wine.status().failed_writes == 1);
}

TEST_CASE("a failed Wine flush counts as a failed write", "[logging][wine]") {
    WineFixture f;
    REQUIRE(f.wine.append(test::session_of(7), "a\n"));
    f.wine.flush();
    CHECK(f.files.flushes() == 1);
    f.files.faults().fail_next(LogFsOperation::Flush, test::fault());
    f.wine.flush();
    CHECK(f.wine.status().failed_writes == 1);
    CHECK(f.in_use.snapshot().empty());
}

TEST_CASE("destroying WineLogFiles releases every open Wine log", "[logging][wine]") {
    ManualClock clock;
    InMemoryLogFileSystem files(clock);
    LogFilesInUse in_use;
    const NativePath dir = NativePath("/logs").make_preferred();
    REQUIRE(files.create_directories(dir));
    {
        WineLogFiles wine(WineLogOptions{dir, test::kGroup}, files, in_use);
        REQUIRE(wine.append(test::session_of(1), "a\n"));
        REQUIRE(wine.append(test::session_of(2), "b\n"));
        CHECK(in_use.snapshot().size() == 2);
    }
    CHECK(in_use.snapshot().empty());
}

TEST_CASE("close_session racing the writer loses no line", "[logging][wine][race]") {
    WineFixture f;
    const SessionId session = test::session_of(8);
    constexpr int kLines = 2000;
    std::thread writer([&] {
        for (int i = 0; i < kLines; ++i) REQUIRE(f.wine.append(session, "line\n"));
    });
    for (int i = 0; i < kLines; ++i) f.wine.close_session(session);
    writer.join();
    const auto text = f.files.text(f.path_of(session));
    REQUIRE(text);
    CHECK(text->size() == kLines * std::string("line\n").size());
}

TEST_CASE("a truncation line that leaves the file under the cap still ends it after a reopen", "[logging][wine]") {
    WineFixture f(200);
    const SessionId session = test::session_of(9);
    const std::string first = std::string(99, 'a') + "\n";
    REQUIRE(f.wine.append(session, first));
    REQUIRE(f.wine.append(session, std::string(149, 'b') + "\n"));
    const std::string truncated = first + "[wine log reached its 200 byte cap; later lines are dropped]\n";
    REQUIRE(truncated.size() < 200);
    CHECK(f.files.text(f.path_of(session)) == truncated);

    f.wine.close_session(session);
    CHECK(f.in_use.snapshot().empty());
    REQUIRE(f.wine.append(session, "y\n"));
    CHECK(f.files.text(f.path_of(session)) == truncated);
    CHECK(f.wine.status().dropped_lines == 2);
}
