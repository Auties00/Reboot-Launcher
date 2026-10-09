#pragma once

#include <string>
#include <string_view>

namespace rb::process {

class BuiltEnv;
struct ProcessSpec;

inline constexpr std::string_view kMasked = "***";

// Capabilities: game-launch.process-spawn-helper.
// The command line as quote_windows_args builds it, with every -AUTH_PASSWORD= value (any case,
// quoted or not) replaced by kMasked. The Redactor still runs over the result.
[[nodiscard]] std::string to_log_string(const ProcessSpec& spec);

// One NAME=VALUE per line in block order, sensitive values replaced by kMasked.
[[nodiscard]] std::string to_log_string(const BuiltEnv& env);

}  // namespace rb::process
