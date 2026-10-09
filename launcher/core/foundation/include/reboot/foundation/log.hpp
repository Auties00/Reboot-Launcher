#pragma once

#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot {

enum class LogLevel : u8 { Trace, Debug, Info, Warn, Error };

enum class LogCategory : u8 { Engine, Ipc, Net, Storage, Builds, Play, Host, Backend, GameOutput, Wine, Browser, Update, Client, Ui };

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
    ~Redactor();
    Redactor(const Redactor&) = delete;
    Redactor& operator=(const Redactor&) = delete;

    void add_secret(std::span<const u8> value);
    void remove_secret(std::span<const u8> value);
    [[nodiscard]] std::string apply(std::string_view text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The one process-wide service. A bounded MPSC queue feeds a single writer thread; when it is
// full, GameOutput, Wine, Debug and Trace records drop first (counted). Warn and above never drop;
// past twice the budget their producers wait for the writer.
class Logger {
public:
    static void install(std::size_t byte_budget);
    static void add_sink(std::unique_ptr<LogSink> sink);
    static Redactor& redactor();

    static void set_level(LogLevel minimum);
    [[nodiscard]] static bool enabled(LogLevel level) noexcept;

    static void write(LogLevel level, LogCategory category, std::optional<SessionId> session, std::string text);
    static void flush();
    // Drains the queue synchronously and stops the writer.
    static void shutdown();
    // The std::terminate path: on the calling thread, writes the queue then one Error line to the sinks
    // and flushes them, never waiting on the writer for long.
    static void drain_for_terminate(LogCategory category, std::string text) noexcept;
};

}  // namespace reboot

#define REBOOT_LOG_AT(level, category, session, ...)                                                       \
    do {                                                                                                      \
        if (::reboot::Logger::enabled(level))                                                                 \
            ::reboot::Logger::write(level, ::reboot::LogCategory::category, session, std::format(__VA_ARGS__)); \
    } while (false)

#define REBOOT_LOG_TRACE(category, ...) REBOOT_LOG_AT(::reboot::LogLevel::Trace, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_DEBUG(category, ...) REBOOT_LOG_AT(::reboot::LogLevel::Debug, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_INFO(category, ...) REBOOT_LOG_AT(::reboot::LogLevel::Info, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_WARN(category, ...) REBOOT_LOG_AT(::reboot::LogLevel::Warn, category, std::nullopt, __VA_ARGS__)
#define REBOOT_LOG_ERROR(category, ...) REBOOT_LOG_AT(::reboot::LogLevel::Error, category, std::nullopt, __VA_ARGS__)
