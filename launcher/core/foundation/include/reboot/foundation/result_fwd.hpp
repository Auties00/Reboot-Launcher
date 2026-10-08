#pragma once

#include <expected>

namespace reboot {

// Lets value-type headers declare Result-returning functions without including diag.hpp,
// which itself depends on those value types.
struct Diagnostic;

template <class T>
using Result = std::expected<T, Diagnostic>;

}  // namespace reboot
