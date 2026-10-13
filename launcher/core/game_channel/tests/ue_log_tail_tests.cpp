#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "game_channel_test_kit.hpp"
#include "reboot/game_channel/ue_log_tail.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::game_channel {
namespace {

using namespace std::chrono_literals;
using test::TestStrand;

[[nodiscard]] Diagnostic failure(std::string id, ErrorKind kind) {
    Diagnostic diag;
    diag.id = std::move(id);
    diag.kind = kind;
    return diag;
}

// One log file whose id, size and failures the test sets; only read_shared is served.
class ScriptedLog final : public ports::IFileSystem {
public:
    void write(std::string_view text, u64 id) {
        const std::scoped_lock lock(mutex_);
        bytes_.assign(text.begin(), text.end());
        id_ = id;
    }
    void append(std::string_view text) {
        const std::scoped_lock lock(mutex_);
        if (!id_) id_ = 1;
        bytes_.insert(bytes_.end(), text.begin(), text.end());
    }
    void remove() {
        const std::scoped_lock lock(mutex_);
        id_.reset();
        bytes_.clear();
    }
    void fail_reads(bool fail) {
        const std::scoped_lock lock(mutex_);
        failing_ = fail;
    }

    Result<ports::SharedRead> read_shared(const NativePath&, u64 offset, std::size_t max_bytes) override {
        const std::scoped_lock lock(mutex_);
        if (failing_) return std::unexpected(failure("test.denied", ErrorKind::Generic));
        if (!id_) return std::unexpected(failure("test.missing", ErrorKind::NotFound));
        ports::SharedRead read;
        read.revision.size = bytes_.size();
        read.revision.file_id = *id_;
        if (offset < bytes_.size()) {
            const std::size_t count = std::min<std::size_t>(max_bytes, bytes_.size() - static_cast<std::size_t>(offset));
            read.bytes.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                              bytes_.begin() + static_cast<std::ptrdiff_t>(offset + count));
        }
        return read;
    }

    Result<void> atomic_replace(const NativePath&, std::span<const u8>, bool) override { return unused(); }
    Result<std::vector<u8>> read_all(const NativePath&) override { return unused(); }
    Result<ports::FileLock> lock_exclusive(const NativePath&, bool) override { return unused(); }
    Result<void> restrict_to_owner(const NativePath&) override { return unused(); }
    Result<ports::HeldFile> open_deny_write(const NativePath&) override { return unused(); }
    Result<ports::FileRevision> revision(const NativePath&) override { return unused(); }
    Result<void> create_dirs_owner_only(const NativePath&) override { return unused(); }
    Result<void> remove_tree(const NativePath&) override { return unused(); }

private:
    [[nodiscard]] static std::unexpected<Diagnostic> unused() {
        return std::unexpected(failure("test.unused", ErrorKind::Unsupported));
    }

    std::mutex mutex_;
    std::vector<u8> bytes_;
    std::optional<u64> id_;
    bool failing_ = false;
};

// Counts game_channel.log_unreadable warnings for as long as it lives.
class UnreadableLogCapture {
public:
    UnreadableLogCapture() {
        Logger::add_sink(std::make_unique<Sink>(counted_));
        Logger::install(1 << 20);
        Logger::set_level(LogLevel::Info);
    }
    ~UnreadableLogCapture() { Logger::shutdown(); }
    UnreadableLogCapture(const UnreadableLogCapture&) = delete;
    UnreadableLogCapture& operator=(const UnreadableLogCapture&) = delete;

    [[nodiscard]] int count() const {
        Logger::flush();
        return counted_->load();
    }

private:
    class Sink final : public LogSink {
    public:
        explicit Sink(std::shared_ptr<std::atomic<int>> counted) : counted_(std::move(counted)) {}
        void write(std::span<const LogRecord> records) override {
            for (const LogRecord& record : records)
                if (record.text.starts_with("game_channel.log_unreadable")) counted_->fetch_add(1);
        }
        void flush() override {}

    private:
        std::shared_ptr<std::atomic<int>> counted_;
    };

    std::shared_ptr<std::atomic<int>> counted_ = std::make_shared<std::atomic<int>>(0);
};

struct Fixture {
    Fixture() {
        tail = std::make_unique<UeLogTail>(log, workers, strand, timers, NativePath("FortniteGame.log"),
                                           [this](std::span<const u8> bytes) { received.append(bytes.begin(), bytes.end()); });
    }
    ~Fixture() {
        tail.reset();
        workers.shutdown();
    }

    void start() {
        bool ready = false;
        tail->start([&] { ready = true; });
        strand.run_until([&] { return ready; });
    }

    // Until the read in flight is applied and the next poll is armed.
    void settle() {
        strand.run_until([&] { return strand.idle(); });
    }

    // One poll interval, then until `text` has arrived.
    void poll_until(std::string_view text) {
        strand.advance(kUeLogPollInterval);
        strand.run_until([&] { return received == text; });
    }

    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    ScriptedLog log;
    WorkerPool workers{1};
    std::string received;
    std::unique_ptr<UeLogTail> tail;
};

TEST_CASE("only bytes written after start are delivered", "[game_channel][ue_log]") {
    Fixture f;
    f.log.write("old run: [UOnlineAccountCommon::ContinueLoggingIn] (Completed)\n", 1);
    f.start();
    CHECK(f.received.empty());
    f.log.append("new line\n");
    f.poll_until("new line\n");
    f.log.append("more\n");
    f.poll_until("new line\nmore\n");
}

TEST_CASE("a missing log is waited for and then read whole", "[game_channel][ue_log]") {
    Fixture f;
    f.start();
    f.log.write("first\n", 4);
    f.poll_until("first\n");
}

TEST_CASE("a new file id or a shorter file restarts at offset zero", "[game_channel][ue_log]") {
    Fixture f;
    f.log.write("previous\n", 1);
    f.start();
    f.log.append("a\n");
    f.poll_until("a\n");

    f.log.write("rotated\n", 2);
    f.poll_until("a\nrotated\n");

    f.log.write("x\n", 2);
    f.poll_until("a\nrotated\nx\n");

    f.log.remove();
    f.log.write("back\n", 3);
    f.poll_until("a\nrotated\nx\nback\n");
}

TEST_CASE("an unreadable log still lets the game launch and is read once it recovers", "[game_channel][ue_log]") {
    const UnreadableLogCapture logged;
    Fixture f;
    f.log.write("stale\n", 1);
    f.log.fail_reads(true);
    f.start();
    f.settle();
    for (int poll = 0; poll < 3; ++poll) {
        f.strand.advance(kUeLogPollInterval);
        f.settle();
    }
    CHECK(logged.count() == 1);

    f.log.fail_reads(false);
    f.strand.advance(kUeLogPollInterval);
    f.settle();
    CHECK(f.received.empty());
    f.log.append("fresh\n");
    f.poll_until("fresh\n");

    // A later outage is a new one.
    f.log.fail_reads(true);
    for (int poll = 0; poll < 2; ++poll) {
        f.strand.advance(kUeLogPollInterval);
        f.settle();
    }
    CHECK(logged.count() == 2);
}

TEST_CASE("a backlog longer than one read is drained without waiting for the poll", "[game_channel][ue_log]") {
    Fixture f;
    f.log.write("", 1);
    f.start();
    const std::string backlog(std::size_t{3} << 19, 'x');
    f.log.append(backlog);
    f.poll_until(backlog);
}

TEST_CASE("a read in flight when the tail goes is discarded", "[game_channel][ue_log]") {
    Fixture f;
    f.log.write("x\n", 1);
    bool ready = false;
    f.tail->start([&] { ready = true; });
    f.tail.reset();
    f.workers.shutdown();
    f.strand.drain();
    CHECK_FALSE(ready);
}

}  // namespace
}  // namespace rb::game_channel
