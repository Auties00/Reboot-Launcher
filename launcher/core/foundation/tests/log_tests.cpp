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
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/log.hpp"
#include "reboot/foundation/secret.hpp"

using namespace reboot;

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

// Sinks stay installed for the whole process, so this one only holds the writer while armed.
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
    REQUIRE(texts.size() == 3);
    CHECK(texts[0] == "4 log records were dropped");
    CHECK(records[0].level == LogLevel::Warn);
    CHECK(texts[1] == "warn");
    CHECK(texts[2] == "error");

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
