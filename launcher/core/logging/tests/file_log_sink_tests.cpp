#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logging_test_support.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/logging/error_router.hpp"
#include "reboot/logging/file_log_sink.hpp"
#include "reboot/logging/log_files_in_use.hpp"
#include "reboot/logging/log_format.hpp"
#include "reboot/logging/wine_log_files.hpp"
#include "reboot/ports/log_file_system.hpp"
#include "reboot/testing/in_memory_log_file_system.hpp"

using namespace rb;
using namespace rb::logging;
using namespace std::chrono_literals;
using rb::testing::InMemoryLogFileSystem;
using rb::testing::LogFsOperation;

namespace {

struct SinkFixture {
    SinkFixture() : files(clock) { clock.set_system(std::chrono::system_clock::time_point{test::kGroup.started_at} + 1h); }

    [[nodiscard]] FileLogOptions options() const { return FileLogOptions{dir, test::kGroup, "engine"}; }

    [[nodiscard]] std::unique_ptr<FileLogSink> open(FileLogOptions with, bool with_wine = false) {
        std::unique_ptr<WineLogFiles> wine;
        if (with_wine) wine = std::make_unique<WineLogFiles>(WineLogOptions{dir, test::kGroup}, files, in_use);
        Result<std::unique_ptr<FileLogSink>> sink = FileLogSink::open(std::move(with), clock, files, in_use, std::move(wine));
        REQUIRE(sink);
        (*sink)->set_on_failure([this](const Diagnostic& failure) {
            const std::scoped_lock lock(mutex);
            failures.push_back(failure);
        });
        return std::move(*sink);
    }

    [[nodiscard]] NativePath part(u32 number) const { return dir / session_log_file_name(test::kGroup, "engine", number); }

    [[nodiscard]] std::vector<Diagnostic> reported() {
        const std::scoped_lock lock(mutex);
        return failures;
    }

    ManualClock clock;
    InMemoryLogFileSystem files;
    LogFilesInUse in_use;
    NativePath dir = NativePath("/data/logs").make_preferred();
    std::mutex mutex;
    std::vector<Diagnostic> failures;
};

// Holds every flush until released, standing in for a device that stalls.
class StallingLogFileSystem final : public ports::ILogFileSystem {
public:
    explicit StallingLogFileSystem(ports::ILogFileSystem& inner) : inner_(inner) {}

    Result<void> create_directories(const NativePath& dir) override { return inner_.create_directories(dir); }
    Result<ports::LogFile> open_append(const NativePath& path) override {
        Result<ports::LogFile> opened = inner_.open_append(path);
        if (!opened) return opened;
        const u64 size = opened->size();
        return ports::LogFile(std::make_unique<Handle>(*this, std::move(*opened)), size);
    }
    Result<std::vector<ports::LogDirEntry>> list(const NativePath& dir) override { return inner_.list(dir); }
    Result<void> remove(const NativePath& path) override { return inner_.remove(path); }

    [[nodiscard]] bool wait_stalled() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 20s, [&] { return stalled_; });
    }
    void release() {
        const std::scoped_lock lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

private:
    class Handle final : public ports::LogFile::Handle {
    public:
        Handle(StallingLogFileSystem& owner, ports::LogFile file) : owner_(owner), file_(std::move(file)) {}
        Result<void> append(std::span<const u8> bytes) override { return file_.append(bytes); }
        Result<void> flush() override {
            std::unique_lock lock(owner_.mutex_);
            owner_.stalled_ = true;
            owner_.changed_.notify_all();
            owner_.changed_.wait(lock, [&] { return owner_.released_; });
            return file_.flush();
        }

    private:
        StallingLogFileSystem& owner_;
        ports::LogFile file_;
    };

    ports::ILogFileSystem& inner_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stalled_ = false;
    bool released_ = false;
};

[[nodiscard]] std::string lines_of(const std::vector<LogRecord>& records) {
    std::string text;
    for (const LogRecord& record : records) text += format_log_line(record);
    return text;
}

}  // namespace

TEST_CASE("opening a session log creates the folder and part 0, which stays in use", "[logging][file_sink]") {
    SinkFixture f;
    auto sink = f.open(f.options());
    CHECK(f.files.is_dir(f.dir));
    CHECK(f.files.text(f.part(0)) == "");
    CHECK(f.in_use.snapshot() == std::vector{f.part(0)});
    CHECK(sink->wine_logs() == nullptr);

    const FileLogStatus status = sink->status();
    CHECK(status.current_file == f.part(0));
    CHECK(status.part == 0);
    CHECK(status.failed_writes == 0);
    CHECK_FALSE(status.last_failure);

    sink.reset();
    CHECK(f.in_use.snapshot().empty());
    CHECK(f.files.open_handles(f.part(0)) == 0);
}

TEST_CASE("an invalid role is internal.bug and opens nothing", "[logging][file_sink]") {
    SinkFixture f;
    FileLogOptions options = f.options();
    options.role = "game-server";
    const auto sink = FileLogSink::open(std::move(options), f.clock, f.files, f.in_use, nullptr);
    REQUIRE_FALSE(sink);
    CHECK(sink.error().id == "internal.bug");
    CHECK_FALSE(f.files.exists(f.dir));
}

TEST_CASE("folder and part failures at open carry the port's cause", "[logging][file_sink]") {
    SinkFixture f;
    f.files.faults().fail_next(LogFsOperation::CreateDirectories, test::fault("platform.access_denied"));
    auto no_dir = FileLogSink::open(f.options(), f.clock, f.files, f.in_use, nullptr);
    REQUIRE_FALSE(no_dir);
    CHECK(no_dir.error().id == "logging.directory_failed");
    REQUIRE(no_dir.error().causes.size() == 1);
    CHECK(no_dir.error().causes[0].id == "platform.access_denied");

    f.files.faults().fail_next(LogFsOperation::OpenAppend, test::fault("platform.disk_full"));
    auto no_file = FileLogSink::open(f.options(), f.clock, f.files, f.in_use, nullptr);
    REQUIRE_FALSE(no_file);
    CHECK(no_file.error().id == "logging.open_failed");
    CHECK(f.in_use.snapshot().empty());
}

TEST_CASE("records are written as formatted lines and an existing part is appended to", "[logging][file_sink]") {
    SinkFixture f;
    f.files.put(f.part(0), "earlier\n", f.clock.system_now());
    auto sink = f.open(f.options());
    const std::vector<LogRecord> records{test::record_of("first"), test::record_of("second\nline", LogCategory::Play)};
    sink->write(records);
    CHECK(f.files.text(f.part(0)) == "earlier\n" + lines_of(records));
}

TEST_CASE("a part rolls forward at its size without renaming or truncating", "[logging][file_sink]") {
    SinkFixture f;
    const std::vector<LogRecord> records{test::record_of("one"), test::record_of("two"), test::record_of("six")};
    FileLogOptions options = f.options();
    options.roll_bytes = format_log_line(records[0]).size() * 2;
    auto sink = f.open(std::move(options));

    sink->write(records);
    CHECK(f.files.text(f.part(0)) == lines_of({records[0], records[1]}));
    CHECK(f.files.text(f.part(1)) == lines_of({records[2]}));
    CHECK(f.in_use.snapshot() == std::vector{f.part(1)});
    CHECK(f.files.open_handles(f.part(0)) == 0);
    const FileLogStatus status = sink->status();
    CHECK(status.part == 1);
    CHECK(status.current_file == f.part(1));
}

TEST_CASE("a line longer than the roll size gets a part of its own", "[logging][file_sink]") {
    SinkFixture f;
    FileLogOptions options = f.options();
    options.roll_bytes = 16;
    auto sink = f.open(std::move(options));
    const std::vector<LogRecord> records{test::record_of(std::string(64, 'x')), test::record_of("next")};
    sink->write(records);
    CHECK(f.files.text(f.part(0)) == lines_of({records[0]}));
    CHECK(f.files.text(f.part(1)) == lines_of({records[1]}));
}

TEST_CASE("retention prunes at open and at each roll, never a file in use", "[logging][file_sink]") {
    SinkFixture f;
    const auto now = f.clock.system_now();
    const LogFileGroup oldest{test::kGroup.started_at - std::chrono::hours{72}, 11};
    const NativePath aged = f.dir / "steam-99.log";
    const NativePath surplus = f.dir / session_log_file_name(oldest, "engine", 0);
    const NativePath proton = f.dir / "steam-1234.log";
    const NativePath foreign = f.dir / "notes.txt";
    f.files.put(aged, "aged\n", now - std::chrono::days{15});
    f.files.put(surplus, "surplus\n", now - 1h);
    f.files.put(proton, "proton\n", now - 1h);
    f.files.put(foreign, "keep\n", now - std::chrono::days{400});

    FileLogOptions options = f.options();
    options.retention.max_sessions = 2;
    options.roll_bytes = 8;
    auto sink = f.open(std::move(options));
    CHECK_FALSE(f.files.exists(aged));
    CHECK(f.files.exists(surplus));
    CHECK(f.files.exists(proton));
    CHECK(f.files.exists(foreign));

    // A newer run's file pushes the oldest group out at the next roll.
    const LogFileGroup newer{test::kGroup.started_at - 1h, 12};
    f.files.put(f.dir / session_log_file_name(newer, "engine", 0), "newer\n", now);
    sink->write(std::vector{test::record_of("a"), test::record_of("b")});
    CHECK_FALSE(f.files.exists(surplus));
    CHECK(f.files.exists(f.part(0)));
    CHECK(f.files.exists(f.part(1)));
    CHECK(f.files.exists(foreign));
}

TEST_CASE("pruning errors are ignored", "[logging][file_sink]") {
    SinkFixture f;
    f.files.faults().fail_next(LogFsOperation::List, test::fault());
    auto sink = f.open(f.options());
    sink->write(std::vector{test::record_of("still logged")});
    CHECK(f.files.text(f.part(0)) == lines_of({test::record_of("still logged")}));
    CHECK(f.reported().empty());
}

TEST_CASE("only the first failed write after a success is reported", "[logging][file_sink]") {
    SinkFixture f;
    auto sink = f.open(f.options());
    f.files.faults().fail_next(LogFsOperation::Append, test::fault("platform.disk_full"), 2);

    sink->write(std::vector{test::record_of("lost 1")});
    sink->write(std::vector{test::record_of("lost 2")});
    REQUIRE(f.reported().size() == 1);
    CHECK(f.reported()[0].id == "logging.write_failed");
    REQUIRE(f.reported()[0].causes.size() == 1);
    CHECK(f.reported()[0].causes[0].id == "platform.disk_full");
    FileLogStatus status = sink->status();
    CHECK(status.failed_writes == 2);
    REQUIRE(status.last_failure);
    CHECK(status.last_failure->id == "logging.write_failed");

    sink->write(std::vector{test::record_of("kept")});
    CHECK(f.files.text(f.part(0)) == lines_of({test::record_of("kept")}));
    f.files.faults().fail_next(LogFsOperation::Append, test::fault());
    sink->write(std::vector{test::record_of("lost 3")});
    CHECK(f.reported().size() == 2);
    CHECK(sink->status().failed_writes == 3);
}

TEST_CASE("a part that fails to open at a roll is reported and retried by the next write", "[logging][file_sink]") {
    SinkFixture f;
    FileLogOptions options = f.options();
    options.roll_bytes = 8;
    auto sink = f.open(std::move(options));
    sink->write(std::vector{test::record_of("first")});
    // The roll's open fails, then the same batch's retry.
    f.files.faults().fail_next(LogFsOperation::OpenAppend, test::fault(), 2);

    sink->write(std::vector{test::record_of("lost")});
    REQUIRE(f.reported().size() == 1);
    CHECK(f.reported()[0].id == "logging.open_failed");
    CHECK(f.reported()[0].find_arg("path") != nullptr);
    CHECK(sink->status().current_file == f.part(1));
    CHECK(sink->status().failed_writes == 2);
    CHECK(f.in_use.snapshot().empty());

    sink->write(std::vector{test::record_of("after")});
    CHECK(f.files.text(f.part(1)) == lines_of({test::record_of("after")}));
    CHECK(f.in_use.snapshot() == std::vector{f.part(1)});
}

TEST_CASE("a write failing right after a part finally opens is reported", "[logging][file_sink]") {
    SinkFixture f;
    FileLogOptions options = f.options();
    options.roll_bytes = 8;
    auto sink = f.open(std::move(options));
    sink->write(std::vector{test::record_of("first")});
    f.files.faults().fail_next(LogFsOperation::OpenAppend, test::fault(), 2);
    sink->write(std::vector{test::record_of("lost")});
    REQUIRE(f.reported().size() == 1);

    // The part opens now, so its first failed write starts a new streak.
    f.files.faults().fail_next(LogFsOperation::Append, test::fault("platform.disk_full"));
    sink->write(std::vector{test::record_of("also lost")});
    REQUIRE(f.reported().size() == 2);
    CHECK(f.reported()[1].id == "logging.write_failed");
    CHECK(f.in_use.snapshot() == std::vector{f.part(1)});
}

TEST_CASE("pruning keeps the open part when the folder is not spelled in normal form", "[logging][file_sink]") {
    SinkFixture f;
    f.dir = NativePath("/data/sub/../logs").make_preferred();
    const NativePath normal_dir = f.dir.lexically_normal();
    const NativePath older = normal_dir / session_log_file_name({test::kGroup.started_at - 1h, 7}, "engine", 0);
    f.files.put(older, "older\n", f.clock.system_now() - 1h);
    f.files.put(f.part(0), "earlier\n", f.clock.system_now());

    FileLogOptions options = f.options();
    options.retention.max_total_bytes = 1;
    auto sink = f.open(std::move(options));
    CHECK_FALSE(f.files.exists(older));
    sink->write(std::vector{test::record_of("kept")});
    CHECK(f.files.text(f.part(0)) == "earlier\n" + lines_of({test::record_of("kept")}));
}

TEST_CASE("Wine lines with a session go to their Wine log, others into the part", "[logging][file_sink][wine]") {
    SinkFixture f;
    auto sink = f.open(f.options(), true);
    REQUIRE(sink->wine_logs() != nullptr);
    const SessionId session = test::session_of(9);
    const LogRecord launcher = test::record_of("launcher");
    const LogRecord wine = test::record_of("fixme:wine", LogCategory::Wine, session);
    const LogRecord unscoped = test::record_of("fixme:unscoped", LogCategory::Wine);
    sink->write(std::vector{launcher, wine, unscoped});

    CHECK(f.files.text(f.part(0)) == lines_of({launcher, unscoped}));
    CHECK(f.files.text(f.dir / wine_log_file_name(test::kGroup, session)) == lines_of({wine}));
}

TEST_CASE("without Wine logs a Wine line stays in the part", "[logging][file_sink][wine]") {
    SinkFixture f;
    auto sink = f.open(f.options());
    const LogRecord wine = test::record_of("fixme:wine", LogCategory::Wine, test::session_of(1));
    sink->write(std::vector{wine});
    CHECK(f.files.text(f.part(0)) == lines_of({wine}));
}

TEST_CASE("a Wine log failure is reported once and its lines fall back to the part", "[logging][file_sink][wine]") {
    SinkFixture f;
    auto sink = f.open(f.options(), true);
    const SessionId session = test::session_of(2);
    const NativePath wine_path = f.dir / wine_log_file_name(test::kGroup, session);
    f.files.faults().fail_next(LogFsOperation::OpenAppend, test::fault());

    const LogRecord first = test::record_of("fixme:1", LogCategory::Wine, session);
    const LogRecord second = test::record_of("fixme:2", LogCategory::Wine, session);
    sink->write(std::vector{first});
    sink->write(std::vector{second});
    CHECK(f.files.text(f.part(0)) == lines_of({first, second}));
    REQUIRE(f.reported().size() == 1);
    CHECK(f.reported()[0].id == "logging.open_failed");
    CHECK(sink->status().failed_writes == 0);

    sink->wine_logs()->close_session(session);
    const LogRecord third = test::record_of("fixme:3", LogCategory::Wine, session);
    sink->write(std::vector{third});
    CHECK(f.files.text(wine_path) == lines_of({third}));
    CHECK(f.reported().size() == 1);
}

TEST_CASE("flush reaches the part and the Wine logs, and reports a failed flush", "[logging][file_sink]") {
    SinkFixture f;
    auto sink = f.open(f.options(), true);
    sink->write(std::vector{test::record_of("a"), test::record_of("w", LogCategory::Wine, test::session_of(1))});
    sink->flush();
    CHECK(f.files.flushes() == 2);

    f.files.faults().fail_next(LogFsOperation::Flush, test::fault());
    sink->flush();
    REQUIRE(f.reported().size() == 1);
    CHECK(f.reported()[0].id == "logging.write_failed");
    CHECK(sink->status().failed_writes == 1);
}

TEST_CASE("set_on_failure returns only once the previous callback finished", "[logging][file_sink][race]") {
    SinkFixture f;
    auto opened = FileLogSink::open(f.options(), f.clock, f.files, f.in_use, nullptr);
    REQUIRE(opened);
    FileLogSink& sink = **opened;

    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    std::atomic<bool> replaced{false};
    std::atomic<bool> replaced_while_running{false};
    sink.set_on_failure([&](const Diagnostic&) {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
        replaced_while_running = replaced.load();
    });

    f.files.faults().fail_next(LogFsOperation::Append, test::fault());
    std::thread writer([&] { sink.write(std::vector{test::record_of("x")}); });
    {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, 20s, [&] { return entered; }));
    }
    std::thread replacer([&] {
        sink.set_on_failure({});
        replaced = true;
    });
    {
        const std::scoped_lock lock(mutex);
        released = true;
    }
    changed.notify_all();
    writer.join();
    replacer.join();
    CHECK_FALSE(replaced_while_running);
    CHECK(replaced);
}

TEST_CASE("status does not wait for a stalled file call", "[logging][file_sink][race]") {
    SinkFixture f;
    StallingLogFileSystem stalling(f.files);
    auto opened = FileLogSink::open(f.options(), f.clock, stalling, f.in_use, nullptr);
    REQUIRE(opened);
    FileLogSink& sink = **opened;

    std::thread flusher([&] { sink.flush(); });
    REQUIRE(stalling.wait_stalled());
    std::mutex mutex;
    std::condition_variable changed;
    bool answered = false;
    std::thread reader([&] {
        const FileLogStatus status = sink.status();
        const std::scoped_lock lock(mutex);
        answered = status.current_file == f.part(0);
        changed.notify_all();
    });
    bool answered_while_stalled = false;
    {
        std::unique_lock lock(mutex);
        answered_while_stalled = changed.wait_for(lock, 20s, [&] { return answered; });
    }
    stalling.release();
    flusher.join();
    reader.join();
    CHECK(answered_while_stalled);
}

TEST_CASE("a watching ErrorRouter keeps the sink's failure with the current part as log_ref", "[logging][file_sink]") {
    SinkFixture f;
    auto opened = FileLogSink::open(f.options(), f.clock, f.files, f.in_use, nullptr);
    REQUIRE(opened);
    ManualClock clock;
    ManualExecutor strand(clock);
    {
        ErrorRouter router(strand, clock);
        router.watch(**opened);
        f.files.faults().fail_next(LogFsOperation::Append, test::fault());
        (*opened)->write(std::vector{test::record_of("x")});
        strand.run_all();

        const auto kept = router.background_failures();
        REQUIRE(kept.size() == 1);
        CHECK(kept[0].diag.id == "logging.write_failed");
        CHECK(kept[0].category == LogCategory::Storage);
        REQUIRE(kept[0].diag.log_ref);
        CHECK(kept[0].diag.log_ref->file == display_utf8(f.part(0).filename()));
    }
    // The router unhooked itself; a later failure posts nothing.
    f.files.faults().fail_next(LogFsOperation::Append, test::fault());
    (*opened)->write(std::vector{test::record_of("y")});
    (*opened)->write(std::vector{test::record_of("z")});
    CHECK(strand.run_all() == 0);
}
