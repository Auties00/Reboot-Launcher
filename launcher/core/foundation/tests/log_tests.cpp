#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/base/secret.hpp"
#include "reboot/foundation/log.hpp"

using namespace rb;

namespace {

std::span<const u8> bytes_of(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

struct Captured {
    std::mutex mutex;
    std::vector<LogRecord> records;
    int flushes = 0;

    std::vector<LogRecord> take() {
        const std::lock_guard lock(mutex);
        return std::exchange(records, {});
    }
};

class CaptureSink final : public LogSink {
public:
    explicit CaptureSink(std::shared_ptr<Captured> captured) : captured_(std::move(captured)) {}

    void write(std::span<const LogRecord> records) override {
        const std::lock_guard lock(captured_->mutex);
        captured_->records.insert(captured_->records.end(), records.begin(), records.end());
    }
    void flush() override {
        const std::lock_guard lock(captured_->mutex);
        ++captured_->flushes;
    }

private:
    std::shared_ptr<Captured> captured_;
};

class TrackedSink final : public LogSink {
public:
    explicit TrackedSink(std::shared_ptr<std::atomic<bool>> destroyed) : destroyed_(std::move(destroyed)) {}
    ~TrackedSink() override { destroyed_->store(true); }

    void write(std::span<const LogRecord>) override {}
    void flush() override {}

private:
    std::shared_ptr<std::atomic<bool>> destroyed_;
};

// Holds the writer only while armed.
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool armed = false;
    bool entered = false;
};

class GateSink final : public LogSink {
public:
    explicit GateSink(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}

    void write(std::span<const LogRecord>) override {
        std::unique_lock lock(gate_->mutex);
        if (!gate_->armed) return;
        gate_->entered = true;
        gate_->changed.notify_all();
        gate_->changed.wait(lock, [&] { return !gate_->armed; });
    }
    void flush() override {}

private:
    std::shared_ptr<Gate> gate_;
};

class ThrowingSink final : public LogSink {
public:
    void write(std::span<const LogRecord>) override { throw std::runtime_error("disk full"); }
    void flush() override { throw std::runtime_error("disk full"); }
};

}  // namespace

TEST_CASE("Redactor masks registered secrets by exact match, longest first", "[foundation][log][redact]") {
    Redactor redactor;
    redactor.add_secret(bytes_of("hunter2"));
    redactor.add_secret(bytes_of("hunter2-extended"));
    redactor.add_secret(bytes_of("abc"));
    CHECK(redactor.apply("pw=hunter2 and hunter2-extended") == "pw=*** and ***");
    // Under four bytes is never registered.
    CHECK(redactor.apply("abc") == "abc");
    CHECK(redactor.apply("hunter2hunter2") == "******");
}

TEST_CASE("Redactor masks overlapping secrets whole", "[foundation][log][redact]") {
    Redactor redactor;
    redactor.add_secret(bytes_of("abcd1234"));
    redactor.add_secret(bytes_of("1234wxyz"));
    CHECK(redactor.apply("x abcd1234wxyz y") == "x *** y");
    CHECK(redactor.apply("1234wxyz abcd1234") == "*** ***");
    // A value next to the key rule's span is masked by both without leaking between them.
    redactor.add_secret(bytes_of("PASSWORD=s3cr"));
    CHECK(redactor.apply("-AUTH_PASSWORD=s3cret!") == "-AUTH_***");
}

TEST_CASE("Redactor counts registrations of the same secret", "[foundation][log][redact]") {
    Redactor redactor;
    redactor.add_secret(bytes_of("token-1234"));
    redactor.add_secret(bytes_of("token-1234"));
    redactor.remove_secret(bytes_of("token-1234"));
    CHECK(redactor.apply("token-1234") == "***");
    redactor.remove_secret(bytes_of("token-1234"));
    CHECK(redactor.apply("token-1234") == "token-1234");
}

TEST_CASE("Redactor masks -AUTH_PASSWORD= values, quoted or not", "[foundation][log][redact]") {
    const Redactor redactor;
    CHECK(redactor.apply("game.exe -AUTH_LOGIN=a -AUTH_PASSWORD=s3cret -epicapp=x") ==
          "game.exe -AUTH_LOGIN=a -AUTH_PASSWORD=*** -epicapp=x");
    CHECK(redactor.apply("-auth_password=\"two words\" next") == "-auth_password=\"***\" next");
    CHECK(redactor.apply("-AUTH_PASSWORD=\"unterminated") == "-AUTH_PASSWORD=\"***");
    CHECK(redactor.apply("-AUTH_PASSWORD= -AUTH_PASSWORD=x") == "-AUTH_PASSWORD= -AUTH_PASSWORD=***");
    CHECK(redactor.apply("end -AUTH_PASSWORD=") == "end -AUTH_PASSWORD=");
}

TEST_CASE("Secret never formats its value and wipes on move", "[foundation][log][secret]") {
    SecretString secret(std::string("p4ssword"));
    CHECK(std::format("{}", secret) == "***");
    CHECK(secret.reveal() == "p4ssword");
    SecretString moved = std::move(secret);
    CHECK(moved.reveal() == "p4ssword");
    CHECK(secret.reveal().empty());
}

TEST_CASE("Logger redacts, drops by priority under pressure and drains on shutdown", "[foundation][log][logger]") {
    CHECK_FALSE(Logger::enabled(LogLevel::Error));
    const auto captured = std::make_shared<Captured>();
    Logger::add_sink(std::make_unique<ThrowingSink>());
    Logger::add_sink(std::make_unique<CaptureSink>(captured));
    Logger::install(1 << 20);
    Logger::set_level(LogLevel::Debug);
    CHECK(Logger::enabled(LogLevel::Debug));
    CHECK_FALSE(Logger::enabled(LogLevel::Trace));

    Logger::redactor().add_secret(bytes_of("tok-secret-value"));
    const SessionId session{Uuid{{3}}};
    REBOOT_LOG_INFO(Net, "connecting with {}", "tok-secret-value");
    REBOOT_LOG_AT(LogLevel::Warn, Play, session, "launch -AUTH_PASSWORD={} done", "pw");
    REBOOT_LOG_TRACE(Net, "below the level");
    Logger::flush();
    auto records = captured->take();
    REQUIRE(records.size() == 2);
    CHECK(records[0].text == "connecting with ***");
    CHECK(records[0].category == LogCategory::Net);
    CHECK(records[0].level == LogLevel::Info);
    CHECK(records[1].text == "launch -AUTH_PASSWORD=*** done");
    CHECK(records[1].session == session);
    CHECK(captured->flushes >= 1);

    // A one-byte budget is always full: only Warn and above get through, then the drop count.
    Logger::install(1);
    Logger::write(LogLevel::Debug, LogCategory::Engine, std::nullopt, "debug");
    Logger::write(LogLevel::Info, LogCategory::GameOutput, std::nullopt, "game");
    Logger::write(LogLevel::Info, LogCategory::Wine, std::nullopt, "wine");
    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "info");
    Logger::write(LogLevel::Warn, LogCategory::Engine, std::nullopt, "warn");
    Logger::write(LogLevel::Error, LogCategory::GameOutput, std::nullopt, "error");
    Logger::shutdown();
    CHECK_FALSE(Logger::enabled(LogLevel::Error));

    records = captured->take();
    std::vector<std::string> texts;
    for (const LogRecord& record : records) texts.push_back(record.text);
    REQUIRE(texts.size() == 5);
    CHECK(texts[0] == "[diag] 2 dropped (engine)");
    CHECK(records[0].level == LogLevel::Warn);
    CHECK(records[0].category == LogCategory::Engine);
    CHECK(texts[1] == "[diag] 1 dropped (game_output)");
    CHECK(texts[2] == "[diag] 1 dropped (wine)");
    CHECK(texts[3] == "warn");
    CHECK(texts[4] == "error");

    // Writes after shutdown go nowhere and do not block.
    Logger::write(LogLevel::Error, LogCategory::Engine, std::nullopt, "late");
    Logger::flush();
    CHECK(captured->take().empty());
    Logger::redactor().remove_secret(bytes_of("tok-secret-value"));
    Logger::set_level(LogLevel::Info);
}

TEST_CASE("Past the hard cap a Warn producer waits for the writer", "[foundation][log][logger]") {
    const auto gate = std::make_shared<Gate>();
    Logger::add_sink(std::make_unique<GateSink>(gate));
    // A one-byte budget makes every Warn exceed the cap whenever the queue holds anything.
    Logger::install(1);
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = true;
    }
    Logger::write(LogLevel::Warn, LogCategory::Engine, std::nullopt, "held by the writer");
    {
        std::unique_lock lock(gate->mutex);
        gate->changed.wait(lock, [&] { return gate->entered; });
    }
    // The writer is busy, so this one stays queued.
    Logger::write(LogLevel::Warn, LogCategory::Engine, std::nullopt, "queued");

    std::atomic<bool> returned{false};
    std::thread producer([&] {
        Logger::write(LogLevel::Error, LogCategory::Engine, std::nullopt, "waits");
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(returned.load());
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = false;
    }
    gate->changed.notify_all();
    producer.join();
    CHECK(returned.load());
    Logger::shutdown();
}

TEST_CASE("A secret removed while a line holding it is queued is still masked in that line", "[foundation][log][redact]") {
    const auto captured = std::make_shared<Captured>();
    const auto gate = std::make_shared<Gate>();
    Logger::add_sink(std::make_unique<GateSink>(gate));
    Logger::add_sink(std::make_unique<CaptureSink>(captured));
    Logger::install(1 << 20);
    Logger::redactor().add_secret(bytes_of("game-token-1234"));
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = true;
    }
    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "held by the writer");
    {
        std::unique_lock lock(gate->mutex);
        gate->changed.wait(lock, [&] { return gate->entered; });
    }
    Logger::write(LogLevel::Info, LogCategory::GameOutput, std::nullopt, "last line with game-token-1234");
    Logger::redactor().remove_secret(bytes_of("game-token-1234"));
    CHECK(Logger::redactor().apply("game-token-1234") == "***");
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = false;
    }
    gate->changed.notify_all();
    Logger::flush();

    const auto records = captured->take();
    REQUIRE(records.size() == 2);
    CHECK(records[1].text == "last line with ***");
    // Once written, the mask goes; with nothing queued a removal is immediate.
    CHECK(Logger::redactor().apply("game-token-1234") == "game-token-1234");
    Logger::redactor().add_secret(bytes_of("idle-token-5678"));
    Logger::redactor().remove_secret(bytes_of("idle-token-5678"));
    CHECK(Logger::redactor().apply("idle-token-5678") == "idle-token-5678");
    Logger::shutdown();
}

TEST_CASE("A secret removed while a Warn producer waits past the hard cap is still masked in its line",
          "[foundation][log][redact]") {
    const auto captured = std::make_shared<Captured>();
    const auto gate = std::make_shared<Gate>();
    Logger::add_sink(std::make_unique<GateSink>(gate));
    Logger::add_sink(std::make_unique<CaptureSink>(captured));
    Logger::install(1);
    Logger::redactor().add_secret(bytes_of("capped-token-1234"));
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = true;
    }
    Logger::write(LogLevel::Warn, LogCategory::Engine, std::nullopt, "held by the writer");
    {
        std::unique_lock lock(gate->mutex);
        gate->changed.wait(lock, [&] { return gate->entered; });
    }
    Logger::write(LogLevel::Warn, LogCategory::Engine, std::nullopt, "queued");
    std::thread producer(
        [] { Logger::write(LogLevel::Error, LogCategory::Engine, std::nullopt, "waits with capped-token-1234"); });
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    Logger::redactor().remove_secret(bytes_of("capped-token-1234"));
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = false;
    }
    gate->changed.notify_all();
    producer.join();
    Logger::flush();

    bool seen = false;
    for (const LogRecord& record : captured->take()) {
        CHECK(record.text.find("capped-token-1234") == std::string::npos);
        seen = seen || record.text == "waits with ***";
    }
    CHECK(seen);
    CHECK(Logger::redactor().apply("capped-token-1234") == "capped-token-1234");
    Logger::shutdown();
}

TEST_CASE("A line whose ticket predates a secret's removal stays masked", "[foundation][log][redact]") {
    const auto captured = std::make_shared<Captured>();
    Logger::add_sink(std::make_unique<CaptureSink>(captured));
    Logger::install(1 << 20);
    Logger::redactor().add_secret(bytes_of("built-token-1234"));
    // Nothing is queued, so without the ticket this removal would be immediate.
    LogTicket ticket = Logger::begin();
    Logger::redactor().remove_secret(bytes_of("built-token-1234"));
    CHECK(Logger::redactor().apply("built-token-1234") == "***");
    Logger::write(std::move(ticket), LogLevel::Info, LogCategory::Engine, std::nullopt, "built with built-token-1234");
    Logger::flush();
    auto records = captured->take();
    REQUIRE(records.size() == 1);
    CHECK(records[0].text == "built with ***");
    CHECK(Logger::redactor().apply("built-token-1234") == "built-token-1234");

    // An unused ticket lets go when destroyed.
    Logger::redactor().add_secret(bytes_of("unused-token-1234"));
    {
        const LogTicket unused = Logger::begin();
        Logger::redactor().remove_secret(bytes_of("unused-token-1234"));
        CHECK(Logger::redactor().apply("unused-token-1234") == "***");
    }
    CHECK(Logger::redactor().apply("unused-token-1234") == "unused-token-1234");

    // A ticket taken after the removal does not hold it back.
    Logger::redactor().add_secret(bytes_of("older-token-1234"));
    LogTicket older = Logger::begin();
    Logger::redactor().remove_secret(bytes_of("older-token-1234"));
    LogTicket newer = Logger::begin();
    Logger::write(std::move(older), LogLevel::Info, LogCategory::Engine, std::nullopt, "older");
    Logger::flush();
    CHECK(Logger::redactor().apply("older-token-1234") == "older-token-1234");
    Logger::write(std::move(newer), LogLevel::Info, LogCategory::Engine, std::nullopt, "newer");
    Logger::flush();
    records = captured->take();
    REQUIRE(records.size() == 2);
    CHECK(records[1].text == "newer");
    Logger::shutdown();
}

TEST_CASE("A terminate drain masks what it takes before the writer can lift the mask", "[foundation][log][redact]") {
    const auto captured = std::make_shared<Captured>();
    const auto gate = std::make_shared<Gate>();
    Logger::add_sink(std::make_unique<GateSink>(gate));
    Logger::add_sink(std::make_unique<CaptureSink>(captured));
    Logger::install(1 << 20);
    Logger::redactor().add_secret(bytes_of("drain-token-1234"));
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = true;
    }
    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "held by the writer");
    {
        std::unique_lock lock(gate->mutex);
        gate->changed.wait(lock, [&] { return gate->entered; });
    }
    Logger::write(LogLevel::Info, LogCategory::GameOutput, std::nullopt, "taken by the drain drain-token-1234");
    Logger::redactor().remove_secret(bytes_of("drain-token-1234"));
    // The drain takes the queue, then waits for the sinks the gated writer holds.
    std::thread terminating([] { Logger::drain_for_terminate(LogCategory::Engine, "terminating"); });
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    // Meanwhile the writer finishes, writes a later line and lifts the mask.
    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "after the drain");
    {
        const std::lock_guard lock(gate->mutex);
        gate->armed = false;
    }
    gate->changed.notify_all();
    terminating.join();
    Logger::flush();

    bool drained = false;
    for (const LogRecord& record : captured->take()) {
        CHECK(record.text.find("drain-token-1234") == std::string::npos);
        drained = drained || record.text == "taken by the drain ***";
    }
    CHECK(drained);
    Logger::shutdown();
}

TEST_CASE("Shutdown destroys the sinks, so a later terminate drain reaches none", "[foundation][log][logger]") {
    const auto destroyed = std::make_shared<std::atomic<bool>>(false);
    const auto captured = std::make_shared<Captured>();
    Logger::add_sink(std::make_unique<TrackedSink>(destroyed));
    Logger::add_sink(std::make_unique<CaptureSink>(captured));
    Logger::install(1 << 20);
    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "before");
    Logger::shutdown();
    CHECK(destroyed->load());
    CHECK(captured->take().size() == 1);

    Logger::drain_for_terminate(LogCategory::Engine, "after shutdown");
    CHECK(captured->take().empty());
}
