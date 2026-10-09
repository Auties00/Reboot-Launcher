#include "reboot/logging/terminate_handler.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <typeinfo>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/logging/log_format.hpp"
#include "terminate_line.hpp"

namespace rb::logging {

namespace {

thread_local std::string_view t_thread_name = "unregistered";

[[noreturn]] void on_terminate() noexcept {
    std::string line;
    try {
        line = terminate_line(registered_thread_name(), current_exception_type());
    } catch (...) {
        line = "internal.bug terminate";
    }
    Logger::drain_for_terminate(LogCategory::Engine, line);
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::abort();
}

}  // namespace

std::string_view registered_thread_name() noexcept { return t_thread_name; }

std::string current_exception_type() {
    const std::exception_ptr current = std::current_exception();
    if (!current) return "none";
    try {
        std::rethrow_exception(current);
    } catch (const std::exception& error) {
        return typeid(error).name();
    } catch (...) {
        return "unknown";
    }
}

std::string terminate_line(std::string_view thread, std::string_view exception_type) {
    return std::format("{} terminate thread={} exception={}", format_diagnostic(internal_bug("terminate")), thread,
                       exception_type);
}

void install_terminate_handler() { std::set_terminate(&on_terminate); }

RegisteredThread::RegisteredThread(std::string_view name) noexcept : previous_(t_thread_name) { t_thread_name = name; }

RegisteredThread::~RegisteredThread() { t_thread_name = previous_; }

}  // namespace rb::logging
