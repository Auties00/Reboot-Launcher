#pragma once

#include <memory>
#include <optional>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/testing/stdio_peer.hpp"

namespace boost::asio {
class io_context;
}

namespace rb::testing {

struct PeerArguments {
    bool describe = false;
    // --script=<file>, else `<own path>.script.json` when that exists.
    std::optional<NativePath> script;
};

// `--control=stdio`, `--describe` where allowed, and `--script=<file>`; anything else prints a
// usage line to stderr and gives nullopt.
[[nodiscard]] std::optional<PeerArguments> parse_peer_arguments(int argc, char** argv, bool allow_describe);
void report_bad_script(const Diagnostic& error);

using PeerFactory =
    UniqueFunction<std::unique_ptr<IStdioPeer>(Executor& executor, const IClock& clock, boost::asio::io_context& io)>;

// Runs a peer as this process: stdin feeds it from a reader thread, a writer thread drains its
// stdout and stderr so it never blocks on them, and its exit code ends the process.
[[noreturn]] void run_peer_process(PeerFactory make);

}  // namespace rb::testing
