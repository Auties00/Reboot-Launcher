#pragma once

#include <span>
#include <string>

namespace reboot::process {

// Capabilities: game-launch.process-spawn-helper.
// The CreateProcessW command line for `argv` (argv[0] is the program), in UTF-8: CommandLineToArgvW
// and the MSVC CRT give `argv` back unchanged, and no shell interprets it. A "-KEY=value" argument
// quotes only its value (-KEY="a b"), the form UE's FParse::Value also reads. No element holds a NUL.
[[nodiscard]] std::string quote_windows_args(std::span<const std::string> argv);

}  // namespace reboot::process
