#pragma once

#include <chrono>
#include <memory>
#include <span>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/child_misbehaviour.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/stdio_peer.hpp"

namespace rb::testing {

// What FakeBackend and FakeGameServer share: framed stdin, liveness, ChildMisbehaviour, and
// timers on the peer's executor that die with it. Executor-only.
class StdioPeerCore : public std::enable_shared_from_this<StdioPeerCore> {
public:
    StdioPeerCore(Executor& executor, const IClock& clock, ChildMisbehaviour misbehaviour);
    virtual ~StdioPeerCore();
    StdioPeerCore(const StdioPeerCore&) = delete;
    StdioPeerCore& operator=(const StdioPeerCore&) = delete;

    void start(StdioPeerOutputs outputs);
    void on_stdin(std::span<const u8> bytes);
    void on_stdin_eof();
    // The owning fake is going away: nothing more is written or run.
    void detach() noexcept { finished_ = true; }

protected:
    virtual void send_hello() = 0;
    // Every frame but Ping, unless the peer hangs or the script marks its type unsupported.
    virtual void handle(const OwnedFrame& frame) = 0;
    // The peer exited: it lets go of what a dead process would, such as its ports.
    virtual void on_finished() {}

    template <ContractMessage T>
    void send(const T& message) {
        write(encode_contract_frame(message));
    }
    void write(std::span<const u8> bytes);
    void write_stderr(std::string_view line);
    // Runs `step` after `delay` on the executor, unless the peer finished first.
    void after(std::chrono::milliseconds delay, UniqueFunction<void()> step);
    // Runs `step` after the script's reply delay, unless the peer hung or finished first.
    void reply(UniqueFunction<void()> step);
    void ok(u64 req_id);
    void refuse(u64 req_id, const Diagnostic& error);
    void unsupported(u64 req_id);
    // Starts the scripted crash and hang timers of `stage`.
    void reached(ScriptStage stage);
    void finish(int exit_code);
    [[nodiscard]] bool finished() const noexcept { return finished_; }

    Executor& executor_;
    const IClock& clock_;
    ChildMisbehaviour misbehaviour_;

private:
    StdioPeerOutputs outputs_;
    Framer framer_{kChildFrameCap};
    bool finished_ = false;
    bool hung_ = false;
};

// Field 1 of a request payload, which every request's req_id is; 0 when it is not there.
[[nodiscard]] u64 first_req_id(std::span<const u8> payload);

}  // namespace rb::testing
