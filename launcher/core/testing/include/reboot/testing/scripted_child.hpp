#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/stdio_peer.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// The test's side of one child spawned by ScriptedProcessLauncher, which keeps it for inspection
// after the parent drops its handle. Output reaches the parent through `io`, as from the I/O thread.
class ScriptedChild {
public:
    // `terminated_exit` is what terminate_tree reports.
    ScriptedChild(Executor& io, u32 pid, std::chrono::system_clock::time_point created, ports::ProcessLaunch launch,
                  ports::ChildExit terminated_exit);
    ~ScriptedChild();
    ScriptedChild(const ScriptedChild&) = delete;
    ScriptedChild& operator=(const ScriptedChild&) = delete;

    // The parent's handle; the launcher hands it out once, from spawn().
    [[nodiscard]] std::unique_ptr<ports::ChildProcess> make_handle();

    [[nodiscard]] u32 pid() const noexcept { return pid_; }
    [[nodiscard]] std::chrono::system_clock::time_point created() const noexcept { return created_; }
    [[nodiscard]] const ports::ProcessLaunch& launch() const noexcept { return launch_; }

    // Nothing is delivered after exit().
    void write_stdout(std::span<const u8> bytes);
    void write_stderr_line(std::string_view line);
    template <ContractMessage T>
    void send(const T& message) {
        const std::vector<u8> frame = encode_contract_frame(message);
        write_stdout(frame);
    }
    void exit(ports::ChildExit exit);

    // Runs `peer` as this child: stdin goes to it, its output comes back as stdout, stderr and exit.
    void attach_peer(std::unique_ptr<IStdioPeer> peer);

    [[nodiscard]] const FrameLog& stdin_frames() const noexcept { return stdin_frames_; }
    [[nodiscard]] bool stdin_closed() const noexcept { return stdin_closed_; }
    [[nodiscard]] bool terminated() const noexcept { return terminated_; }
    [[nodiscard]] bool exited() const noexcept { return exit_.has_value(); }
    [[nodiscard]] bool released() const noexcept { return released_; }

private:
    struct Callbacks;

    Executor& io_;
    u32 pid_;
    std::chrono::system_clock::time_point created_;
    ports::ProcessLaunch launch_;
    ports::ChildExit terminated_exit_;
    FrameLog stdin_frames_{kChildFrameCap};
    std::unique_ptr<IStdioPeer> peer_;
    // Shared with the parent's handle and with posted deliveries.
    std::shared_ptr<Callbacks> callbacks_;
    std::optional<ports::ChildExit> exit_;
    bool stdin_closed_ = false;
    bool terminated_ = false;
    bool released_ = false;
};

}  // namespace reboot::testing
