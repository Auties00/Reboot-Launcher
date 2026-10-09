#include <archive.h>
#include <archive_entry.h>

#include <any>
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "logging_test_support.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/logging/log_exporter.hpp"
#include "reboot/testing/scratch_dir.hpp"

using namespace rb;
using namespace rb::logging;

namespace {

constexpr std::string_view kSecret = "hunter2-secret";

void write_file(const NativePath& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    REQUIRE(out);
}

[[nodiscard]] std::map<std::string, std::string> read_zip(const NativePath& path) {
    std::map<std::string, std::string> entries;
    archive* zip = archive_read_new();
    REQUIRE(zip != nullptr);
    archive_read_support_format_zip(zip);
#ifdef _WIN32
    REQUIRE(archive_read_open_filename_w(zip, path.c_str(), 1 << 16) == ARCHIVE_OK);
#else
    REQUIRE(archive_read_open_filename(zip, path.c_str(), 1 << 16) == ARCHIVE_OK);
#endif
    archive_entry* entry = nullptr;
    while (archive_read_next_header(zip, &entry) == ARCHIVE_OK) {
        std::string data;
        char buffer[4096];
        for (la_ssize_t got; (got = archive_read_data(zip, buffer, sizeof buffer)) > 0;)
            data.append(buffer, static_cast<std::size_t>(got));
        entries.emplace(archive_entry_pathname(entry), std::move(data));
    }
    archive_read_free(zip);
    return entries;
}

struct ExportFixture {
    ExportFixture()
        : scratch([this] {
              Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-log-export");
              REQUIRE(dir);
              return std::move(*dir);
          }()),
          logs(scratch.path() / "logs"),
          strand(clock),
          timers(clock, strand),
          events(EngineEpoch{1}),
          ops(clock, timers, events),
          exporter(logs, redactor, ops, workers, strand) {
        std::filesystem::create_directories(logs);
        const std::string_view text = kSecret;
        redactor.add_secret(std::span(reinterpret_cast<const u8*>(text.data()), text.size()));
    }

    [[nodiscard]] std::optional<ErasedOutcome> wait(OpId op) {
        strand.run_until([&] { return ops.outcome(op).has_value(); });
        return ops.outcome(op);
    }

    OsRandom random;
    testing::ScratchDir scratch;
    NativePath logs;
    ManualClock clock;
    test::WorkerStrand strand;
    TimerService timers;
    EventBus events;
    OpRegistry ops;
    Redactor redactor;
    WorkerPool workers{1};
    LogExporter exporter;
};

[[nodiscard]] LogExportResult result_of(const std::optional<ErasedOutcome>& outcome) {
    REQUIRE(outcome);
    const auto* completed = std::get_if<Completed<std::any>>(&*outcome);
    REQUIRE(completed != nullptr);
    const auto* result = std::any_cast<LogExportResult>(&completed->value);
    REQUIRE(result != nullptr);
    return *result;
}

[[nodiscard]] std::size_t partial_files(const NativePath& dir) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".partial") ++count;
    return count;
}

}  // namespace

TEST_CASE("an export destination must be an absolute .zip outside the logs", "[logging][export]") {
    ExportFixture f;
    for (const NativePath& destination :
         {NativePath("relative.zip"), f.scratch.path() / "logs.tar", f.logs / "inside.zip", f.logs / "sub" / "x.zip"}) {
        const Result<OpHandle> started = f.exporter.start_export({destination, ""}, DisconnectPolicy::Detached);
        REQUIRE_FALSE(started);
        CHECK(started.error().id == "logging.export_destination_invalid");
        CHECK(started.error().kind == ErrorKind::InvalidInput);
    }
    CHECK(f.ops.live().empty());
    CHECK(f.exporter.start_export({f.scratch.path() / "Logs.ZIP", ""}, DisconnectPolicy::Detached));
}

TEST_CASE("the export zips every log kind, redacted, with the runtime summary", "[logging][export]") {
    ExportFixture f;
    const std::string session_log = session_log_file_name(test::kGroup, "engine", 0);
    const std::string wine_log = wine_log_file_name(test::kGroup, test::session_of(1));
    write_file(f.logs / session_log, std::string("token ") + std::string(kSecret) + " here\nplain\n");
    write_file(f.logs / wine_log, "wine -AUTH_PASSWORD=\"quoted pass\" -next\r\n");
    write_file(f.logs / "steam-1234.log", std::string("proton ") + std::string(kSecret));
    write_file(f.logs / "notes.txt", "not a log");
    std::filesystem::create_directories(f.logs / "launcher-dir.log");

    const NativePath destination = f.scratch.path() / "export.zip";
    const Result<OpHandle> started =
        f.exporter.start_export({destination, "engine 1.0\nsecret " + std::string(kSecret)}, DisconnectPolicy::Detached);
    REQUIRE(started);
    const LogExportResult result = result_of(f.wait(started->id()));
    CHECK(result.archive == destination);
    CHECK(result.files == 3);
    CHECK(result.skipped == 0);
    CHECK(result.bytes == std::filesystem::file_size(destination));
    CHECK(partial_files(f.scratch.path()) == 0);

    const auto entries = read_zip(destination);
    REQUIRE(entries.size() == 4);
    CHECK(entries.at("runtime-summary.txt") == "engine 1.0\nsecret ***");
    CHECK(entries.at(session_log) == "token *** here\nplain\n");
    CHECK(entries.at(wine_log) == "wine -AUTH_PASSWORD=\"***\" -next\r\n");
    CHECK(entries.at("steam-1234.log") == "proton ***");
}

TEST_CASE("an existing destination is replaced once the archive is complete", "[logging][export]") {
    ExportFixture f;
    const NativePath destination = f.scratch.path() / "export.zip";
    write_file(destination, "previous export");
    write_file(f.logs / session_log_file_name(test::kGroup, "engine", 0), "line\n");
    const Result<OpHandle> started = f.exporter.start_export({destination, "summary"}, DisconnectPolicy::Detached);
    REQUIRE(started);
    CHECK(result_of(f.wait(started->id())).files == 1);
    CHECK(read_zip(destination).size() == 2);
}

TEST_CASE("a missing logs folder exports only the summary", "[logging][export]") {
    ExportFixture f;
    std::filesystem::remove_all(f.logs);
    const NativePath destination = f.scratch.path() / "export.zip";
    const Result<OpHandle> started = f.exporter.start_export({destination, "summary"}, DisconnectPolicy::Detached);
    REQUIRE(started);
    CHECK(result_of(f.wait(started->id())).files == 0);
    CHECK(read_zip(destination) == std::map<std::string, std::string>{{"runtime-summary.txt", "summary"}});
}

TEST_CASE("an unwritable destination fails and leaves no partial archive", "[logging][export]") {
    ExportFixture f;
    const NativePath destination = f.scratch.path() / "missing" / "export.zip";
    const Result<OpHandle> started = f.exporter.start_export({destination, ""}, DisconnectPolicy::Detached);
    REQUIRE(started);
    const auto outcome = f.wait(started->id());
    REQUIRE(outcome);
    const auto* failed = std::get_if<Failed>(&*outcome);
    REQUIRE(failed != nullptr);
    CHECK(failed->error.id == "logging.export_write_failed");
    CHECK_FALSE(std::filesystem::exists(destination));
    CHECK(partial_files(f.scratch.path()) == 0);
}

TEST_CASE("a cancelled export removes its partial archive and keeps the old destination", "[logging][export]") {
    ExportFixture f;
    const NativePath destination = f.scratch.path() / "export.zip";
    write_file(destination, "previous export");
    write_file(f.logs / session_log_file_name(test::kGroup, "engine", 0), "line\n");

    // Holds the only worker so the cancel lands before the export runs.
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    f.workers.submit<int>(
        [&](CancelToken) -> Result<int> {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return released; });
            return 0;
        },
        {}, f.strand, [](Result<int>) {});

    const Result<OpHandle> started = f.exporter.start_export({destination, ""}, DisconnectPolicy::BoundToConnection);
    REQUIRE(started);
    REQUIRE(f.ops.cancel(started->id(), CancelReason::User));
    {
        const std::scoped_lock lock(mutex);
        released = true;
    }
    changed.notify_all();

    const auto outcome = f.wait(started->id());
    REQUIRE(outcome);
    CHECK(std::holds_alternative<Cancelled>(*outcome));
    // The work's own reply still arrives, ahead of this one, and changes nothing.
    bool trailing_done = false;
    f.workers.submit<int>([](CancelToken) -> Result<int> { return 0; }, {}, f.strand,
                          [&](Result<int>) { trailing_done = true; });
    f.strand.run_until([&] { return trailing_done; });
    CHECK(std::holds_alternative<Cancelled>(*f.ops.outcome(started->id())));
    std::ifstream kept(destination, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(kept), {}) == "previous export");
    CHECK(partial_files(f.scratch.path()) == 0);
}
