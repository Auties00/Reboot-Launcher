#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "win32.hpp"

namespace reboot::os_windows::ipc {

// One I/O completion port and the thread that drains it. The listener owns one for all its
// streams; a client stream owns its own.
class PipeIoThread {
public:
    // A failing CreateIoCompletionPort or thread start is platform.ipc_call_failed.
    [[nodiscard]] static Result<std::unique_ptr<PipeIoThread>> start();
    // Cancels pending I/O on every bound handle and joins the thread.
    ~PipeIoThread();
    PipeIoThread(const PipeIoThread&) = delete;
    PipeIoThread& operator=(const PipeIoThread&) = delete;

    // `completion` runs on the I/O thread, which catches everything it throws as internal.bug.
    using Completion = void (*)(void* owner, OVERLAPPED* overlapped, DWORD bytes, DWORD error);
    [[nodiscard]] Result<void> bind(HANDLE handle, void* owner, Completion completion);

private:
    PipeIoThread();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::os_windows::ipc
