#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "win32.hpp"

namespace rb::os_windows::ipc {

// One I/O completion port and the thread that drains it. The listener owns one for all its
// streams; a client stream owns its own.
class PipeIoThread {
public:
    // One overlapped call or posted packet. `complete` runs on the I/O thread, which catches
    // everything it throws as internal.bug; the request must stay alive until then.
    struct Request {
        OVERLAPPED overlapped{};
        void* owner = nullptr;
        void (*complete)(void* owner, DWORD bytes, DWORD error) = nullptr;
    };

    // An object using the thread, told when the thread stops before the owner detached.
    class Owner {
    public:
        // Closes the owner's handle and forgets the thread; packets posted from here still run.
        virtual void abandon() noexcept = 0;

    protected:
        ~Owner() = default;
    };

    // A failing CreateIoCompletionPort or thread start is platform.ipc_call_failed.
    [[nodiscard]] static Result<std::unique_ptr<PipeIoThread>> start();
    // Abandons every owner still attached, runs the packets still due and joins the thread; from
    // the thread itself it detaches instead.
    ~PipeIoThread();
    PipeIoThread(const PipeIoThread&) = delete;
    PipeIoThread& operator=(const PipeIoThread&) = delete;

    // Associates `handle` with the port; once per handle.
    [[nodiscard]] Result<void> bind(HANDLE handle);
    void attach(std::weak_ptr<Owner> owner);
    void detach(const Owner* owner) noexcept;

    // Counts an overlapped call about to start on a bound handle; false once the thread stops,
    // so no call starts after the owners were abandoned.
    [[nodiscard]] bool begin_io() noexcept;
    // For a counted call that failed without queuing a packet.
    void end_io() noexcept;
    // Queues `request` with error 0; false once the thread has ended.
    [[nodiscard]] bool post(Request& request) noexcept;
    [[nodiscard]] bool on_thread() const noexcept;

private:
    struct State;

    explicit PipeIoThread(std::shared_ptr<State> state);

    std::shared_ptr<State> state_;
};

}  // namespace rb::os_windows::ipc
