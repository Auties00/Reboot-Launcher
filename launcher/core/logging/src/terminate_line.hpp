#pragma once

#include <string>
#include <string_view>

namespace reboot::logging {

// The RegisteredThread name of the calling thread, or "unregistered".
[[nodiscard]] std::string_view registered_thread_name() noexcept;

// The dynamic type of the exception being handled, "none" without one; never its what().
[[nodiscard]] std::string current_exception_type();

[[nodiscard]] std::string terminate_line(std::string_view thread, std::string_view exception_type);

}  // namespace reboot::logging
