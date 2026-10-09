#pragma once

#include <string_view>

namespace rb::logging {

// Capabilities: logging-diagnostics.+41, logging-diagnostics.+51.
// Idempotent. On std::terminate: Logger::drain_for_terminate with one internal.bug line, the line to stderr, abort().
// The line names the thread and exception type, never what(), which the Redactor has not seen.
void install_terminate_handler();

// Capabilities: logging-diagnostics.+41.
// Thread-local name for the terminate line; entry points still catch everything as internal.bug.
class RegisteredThread {
public:
    // `name` must have static storage duration.
    explicit RegisteredThread(std::string_view name) noexcept;
    ~RegisteredThread();
    RegisteredThread(const RegisteredThread&) = delete;
    RegisteredThread& operator=(const RegisteredThread&) = delete;

private:
    std::string_view previous_;
};

}  // namespace rb::logging
