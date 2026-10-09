#pragma once

#include <span>
#include <string_view>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::testing {

// Where a stdio peer's output goes: real stdout/stderr and process exit in a fake executable, a
// ScriptedChild in-process.
struct StdioPeerOutputs {
    UniqueFunction<void(std::span<const u8>)> stdout_bytes;
    UniqueFunction<void(std::string_view line)> stderr_line;
    // The peer is done; nothing is written after it.
    UniqueFunction<void(int exit_code)> exit;
};

// Covers no capability ids (decision testing-strategy).
// The program side of a framed stdio child, so FakeBackend and FakeGameServer run both in-process
// behind ScriptedProcessLauncher and as fake executables.
class IStdioPeer {
public:
    virtual ~IStdioPeer() = default;

    // Called once, before any input.
    virtual void start(StdioPeerOutputs outputs) = 0;
    virtual void on_stdin(std::span<const u8> bytes) = 0;
    // stdin EOF means stop.
    virtual void on_stdin_eof() = 0;
};

}  // namespace rb::testing
