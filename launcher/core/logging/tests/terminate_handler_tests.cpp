#include <catch2/catch_test_macros.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "reboot/foundation/log.hpp"
#include "reboot/logging/terminate_handler.hpp"
#include "terminate_line.hpp"

using namespace rb;
using namespace rb::logging;

namespace {

// Shared with the sink, which the Logger keeps for the rest of the process.
struct Captured {
    std::mutex mutex;
    std::vector<LogRecord> records;
};

class CapturingSink final : public LogSink {
public:
    explicit CapturingSink(std::shared_ptr<Captured> captured) : captured_(std::move(captured)) {}
    void write(std::span<const LogRecord> records) override {
        const std::scoped_lock lock(captured_->mutex);
        captured_->records.insert(captured_->records.end(), records.begin(), records.end());
    }
    void flush() override {}

private:
    std::shared_ptr<Captured> captured_;
};

[[nodiscard]] std::size_t index_of(Captured& captured, std::string_view text) {
    const std::scoped_lock lock(captured.mutex);
    for (std::size_t i = 0; i < captured.records.size(); ++i)
        if (captured.records[i].text.find(text) != std::string::npos) return i;
    return captured.records.size();
}

}  // namespace

TEST_CASE("RegisteredThread names the thread for the terminate line and restores on exit", "[logging][terminate]") {
    CHECK(registered_thread_name() == "unregistered");
    {
        const RegisteredThread strand("strand");
        CHECK(registered_thread_name() == "strand");
        {
            const RegisteredThread nested("worker");
            CHECK(registered_thread_name() == "worker");
        }
        CHECK(registered_thread_name() == "strand");

        std::string_view other;
        std::thread([&] { other = registered_thread_name(); }).join();
        CHECK(other == "unregistered");
    }
    CHECK(registered_thread_name() == "unregistered");
}

TEST_CASE("the terminate line names the exception type, never its message", "[logging][terminate]") {
    CHECK(current_exception_type() == "none");
    std::string type;
    try {
        throw std::runtime_error("password=hunter2");
    } catch (...) {
        type = current_exception_type();
    }
    CHECK(type.find("runtime_error") != std::string::npos);
    try {
        throw 42;
    } catch (...) {
        type = current_exception_type();
    }
    CHECK(type == "unknown");

    const std::string line = terminate_line("strand", "std::runtime_error");
    CHECK(line.starts_with("internal.bug("));
    CHECK(line.find("thread=strand") != std::string::npos);
    CHECK(line.find("exception=std::runtime_error") != std::string::npos);
}

TEST_CASE("installing the terminate handler is idempotent", "[logging][terminate]") {
    install_terminate_handler();
    const std::terminate_handler first = std::get_terminate();
    install_terminate_handler();
    CHECK(std::get_terminate() == first);
}

TEST_CASE("the terminate drain writes queued records and the redacted line", "[logging][terminate]") {
    Logger::install(1u << 20);
    auto captured = std::make_shared<Captured>();
    Logger::add_sink(std::make_unique<CapturingSink>(captured));
    constexpr std::string_view kSecret = "terminate-secret";
    Logger::redactor().add_secret(std::span(reinterpret_cast<const u8*>(kSecret.data()), kSecret.size()));

    Logger::write(LogLevel::Info, LogCategory::Engine, std::nullopt, "queued before terminate");
    Logger::drain_for_terminate(LogCategory::Engine, "internal.bug terminate terminate-secret");
    const std::size_t last = index_of(*captured, "internal.bug terminate ***");
    // The writer may have taken the queued record just before the drain; it still lands.
    Logger::flush();
    const std::size_t queued = index_of(*captured, "queued before terminate");

    const std::scoped_lock lock(captured->mutex);
    REQUIRE(last < captured->records.size());
    CHECK(captured->records[last].level == LogLevel::Error);
    CHECK(captured->records[last].text.find("terminate-secret") == std::string::npos);
    CHECK(queued < captured->records.size());
    Logger::redactor().remove_secret(std::span(reinterpret_cast<const u8*>(kSecret.data()), kSecret.size()));
}
