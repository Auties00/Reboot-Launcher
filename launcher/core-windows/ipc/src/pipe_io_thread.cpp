#include "pipe_io_thread.hpp"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "reboot/foundation/log.hpp"
#include "win32_errors.hpp"

namespace rb::os_windows::ipc {

struct PipeIoThread::State {
    explicit State(HANDLE completion_port) : port(completion_port) {}
    ~State() { CloseHandle(port); }
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    const HANDLE port;
    std::thread thread;
    std::atomic<DWORD> thread_id{0};

    std::mutex mutex;
    std::unordered_map<const Owner*, std::weak_ptr<Owner>> owners;
    // Counted calls and posted packets whose completion has not run yet.
    std::size_t pending = 0;
    bool stopping = false;
    bool ended = false;

    void run() noexcept;
    // Ends the loop once stopping and nothing is pending.
    [[nodiscard]] bool finished_one(bool counted) noexcept;
};

void PipeIoThread::State::run() noexcept {
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        const BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, INFINITE);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (overlapped == nullptr && !ok) {
            REBOOT_LOG_ERROR(Ipc, "the engine pipe I/O thread stopped: GetQueuedCompletionStatus failed with {}", error);
            const std::lock_guard lock{mutex};
            ended = true;
            return;
        }
        if (overlapped != nullptr) {
            Request* request = CONTAINING_RECORD(overlapped, Request, overlapped);
            try {
                // The request may be freed by its own completion, so nothing reads it afterwards.
                request->complete(request->owner, bytes, error);
            } catch (...) {
                REBOOT_LOG_ERROR(Ipc, "{} on the engine pipe I/O thread", internal_bug("os_windows.pipe_io_thread").id);
            }
        }
        if (finished_one(overlapped != nullptr)) return;
    }
}

bool PipeIoThread::State::finished_one(bool counted) noexcept {
    const std::lock_guard lock{mutex};
    if (counted) --pending;
    if (!stopping || pending != 0) return false;
    ended = true;
    return true;
}

Result<std::unique_ptr<PipeIoThread>> PipeIoThread::start() {
    const HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (port == nullptr) return std::unexpected(call_failed("CreateIoCompletionPort"));
    auto state = std::make_shared<State>(port);
    try {
        state->thread = std::thread([state] {
            state->thread_id = GetCurrentThreadId();
            state->run();
        });
    } catch (const std::system_error& error) {
        return std::unexpected(call_failed("CreateThread", static_cast<DWORD>(error.code().value())));
    }
    return std::unique_ptr<PipeIoThread>(new PipeIoThread(std::move(state)));
}

PipeIoThread::PipeIoThread(std::shared_ptr<State> state) : state_(std::move(state)) {}

PipeIoThread::~PipeIoThread() {
    std::vector<std::weak_ptr<Owner>> owners;
    {
        const std::lock_guard lock{state_->mutex};
        state_->stopping = true;
        for (const auto& [key, owner] : state_->owners) owners.push_back(owner);
    }
    for (const std::weak_ptr<Owner>& weak : owners)
        if (const std::shared_ptr<Owner> owner = weak.lock()) owner->abandon();
    // Wakes the loop so it sees `stopping` even with nothing pending.
    PostQueuedCompletionStatus(state_->port, 0, 0, nullptr);
    if (on_thread())
        state_->thread.detach();
    else
        state_->thread.join();
}

Result<void> PipeIoThread::bind(HANDLE handle) {
    if (CreateIoCompletionPort(handle, state_->port, 0, 0) == nullptr)
        return std::unexpected(call_failed("CreateIoCompletionPort"));
    return {};
}

void PipeIoThread::attach(std::weak_ptr<Owner> owner) {
    const std::shared_ptr<Owner> strong = owner.lock();
    if (!strong) return;
    const std::lock_guard lock{state_->mutex};
    state_->owners.insert_or_assign(strong.get(), std::move(owner));
}

void PipeIoThread::detach(const Owner* owner) noexcept {
    const std::lock_guard lock{state_->mutex};
    state_->owners.erase(owner);
}

bool PipeIoThread::begin_io() noexcept {
    const std::lock_guard lock{state_->mutex};
    if (state_->stopping) return false;
    ++state_->pending;
    return true;
}

void PipeIoThread::end_io() noexcept {
    {
        const std::lock_guard lock{state_->mutex};
        --state_->pending;
    }
    // The loop may be waiting for this count to reach zero.
    PostQueuedCompletionStatus(state_->port, 0, 0, nullptr);
}

bool PipeIoThread::post(Request& request) noexcept {
    {
        const std::lock_guard lock{state_->mutex};
        if (state_->ended) return false;
        ++state_->pending;
    }
    request.overlapped = OVERLAPPED{};
    if (PostQueuedCompletionStatus(state_->port, 0, 0, &request.overlapped)) return true;
    end_io();
    return false;
}

bool PipeIoThread::on_thread() const noexcept { return GetCurrentThreadId() == state_->thread_id.load(); }

}  // namespace rb::os_windows::ipc
