#pragma once

#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "reboot/base/log_level.hpp"
#include "reboot/base/types.hpp"

namespace rb {

// "engine", "game_output" and so on, as log lines print it.
[[nodiscard]] std::string_view log_category_name(LogCategory category) noexcept;

struct LogRecord {
    std::chrono::system_clock::time_point time;
    LogLevel level{};
    LogCategory category{};
    std::optional<SessionId> session;
    std::string text;
};

// Called only on the logger's writer thread, with already redacted records.
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(std::span<const LogRecord> records) = 0;
    virtual void flush() = 0;
};

// Thread-safe. Masks every registered secret by exact match, and the value after
// -AUTH_PASSWORD= (quoted or not). Values shorter than 4 bytes are not registered.
class Redactor {
public:
    Redactor();
    virtual ~Redactor();
    Redactor(const Redactor&) = delete;
    Redactor& operator=(const Redactor&) = delete;

    void add_secret(std::span<const u8> value);
    // On Logger::redactor() the secret stays masked until the writer has written every record
    // whose LogTicket was taken before the call; any other Redactor stops masking it at once.
    virtual void remove_secret(std::span<const u8> value);
    [[nodiscard]] std::string apply(std::string_view text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Taken before a line's text is built: a remove_secret made after it still masks that line.
// One not handed to Logger::write releases its hold when destroyed.
class LogTicket {
public:
    LogTicket() = default;
    LogTicket(LogTicket&& other) noexcept;
    LogTicket& operator=(LogTicket&& other) noexcept;
    LogTicket(const LogTicket&) = delete;
    LogTicket& operator=(const LogTicket&) = delete;
    ~LogTicket();

private:
    friend class Logger;
    explicit LogTicket(u64 epoch) : epoch_(epoch), held_(true) {}

    u64 epoch_ = 0;
    bool held_ = false;
};

// The one process-wide service. A bounded MPSC queue feeds a single writer thread; when it is
// full, GameOutput, Wine, Debug and Trace records drop first, counted per category and reported
// as "[diag] N dropped (category)". Warn and above never drop; past twice the budget their
// producers wait for the writer.
class Logger {
public:
    static void install(std::size_t byte_budget);
    static void add_sink(std::unique_ptr<LogSink> sink);
    static Redactor& redactor();

    static void set_level(LogLevel minimum);
    [[nodiscard]] static bool enabled(LogLevel level) noexcept;

    [[nodiscard]] static LogTicket begin();
    static void write(LogTicket ticket, LogLevel level, LogCategory category, std::optional<SessionId> session,
                      std::string text);
    // Takes its ticket on entry, so `text` must not hold a secret removed before the call.
    static void write(LogLevel level, LogCategory category, std::optional<SessionId> session, std::string text);
    static void flush();
    // Drains the queue synchronously, stops the writer and destroys the sinks.
    static void shutdown();
    // The std::terminate path: on the calling thread, writes the queue then one Error line to the sinks
    // and flushes them, never waiting on the writer for long.
    static void drain_for_terminate(LogCategory category, std::string text) noexcept;
};

}  // namespace rb

#define REBOOT_LOG_AT(level, category, session, ...)                                                    \
    do {                                                                                                \
        if (::rb::Logger::enabled(level)) {                                                             \
            ::rb::LogTicket rb_log_ticket_ = ::rb::Logger::begin();                                     \
            ::rb::Logger::write(std::move(rb_log_ticket_), level, ::rb::LogCategory::category, session, \
                                std::format(__VA_ARGS__));                                              \
        }                                                                                               \
    } while (false)

#define REBOOT_LOG_TRACE(category, ...) REBOOT_LOG_AT(::rb::LogLevel::Trace, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_DEBUG(category, ...) REBOOT_LOG_AT(::rb::LogLevel::Debug, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_INFO(category, ...) REBOOT_LOG_AT(::rb::LogLevel::Info, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_WARN(category, ...) REBOOT_LOG_AT(::rb::LogLevel::Warn, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_ERROR(category, ...) REBOOT_LOG_AT(::rb::LogLevel::Error, category, std::nullopt, __VA_ARGS__)
