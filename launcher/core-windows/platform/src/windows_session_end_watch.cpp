#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_session_end_watch.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

#include "reboot/foundation/log.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

struct SessionEndState {
    std::chrono::milliseconds budget;
    UniqueFunction<void()> on_end;
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
    std::optional<Diagnostic> failure;
    HWND window = nullptr;
    bool ended = false;
    bool released = false;
};

namespace {

// Shown by Windows while the engine holds the logoff or shutdown.
constexpr const wchar_t* kBlockReason = L"Reboot Launcher is stopping its game servers and saving its state.";

void hold(SessionEndState& state) {
    UniqueFunction<void()> on_end;
    {
        const std::scoped_lock lock(state.mutex);
        if (!state.ended) on_end = std::move(state.on_end);
        state.ended = true;
    }
    if (on_end) {
        try {
            on_end();
        } catch (...) {
            REBOOT_LOG_ERROR(Engine, "internal.bug: a session end callback threw");
        }
    }
    std::unique_lock lock(state.mutex);
    state.changed.wait_for(lock, state.budget, [&] { return state.released; });
}

LRESULT CALLBACK session_end_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<SessionEndState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
        case WM_QUERYENDSESSION:
            ShutdownBlockReasonCreate(window, kBlockReason);
            return TRUE;
        case WM_ENDSESSION:
            // Windows terminates the process once this returns, so the shutdown runs inside it.
            if (wparam != FALSE && state != nullptr) hold(*state);
            ShutdownBlockReasonDestroy(window);
            return 0;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window, message, wparam, lparam);
    }
}

void run_window(SessionEndState& state) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof window_class;
    window_class.lpfnWndProc = session_end_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = kSessionEndWindowClass;
    std::optional<Diagnostic> failure;
    HWND window = nullptr;
    if (RegisterClassExW(&window_class) == 0) {
        if (const DWORD error = GetLastError(); error != ERROR_CLASS_ALREADY_EXISTS) failure = call_failed("RegisterClassExW", error);
    }
    if (!failure) {
        // Top-level but never shown: message-only windows miss the session broadcast.
        window = CreateWindowExW(0, kSessionEndWindowClass, L"Reboot Launcher", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                 instance, &state);
        if (window == nullptr) failure = call_failed("CreateWindowExW", GetLastError());
    }
    {
        const std::scoped_lock lock(state.mutex);
        state.window = window;
        state.failure = std::move(failure);
        state.ready = true;
    }
    state.changed.notify_all();
    if (window == nullptr) return;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

WindowsSessionEndWatch::WindowsSessionEndWatch(std::chrono::milliseconds budget)
    : state_(std::make_unique<SessionEndState>()) {
    state_->budget = budget;
}

WindowsSessionEndWatch::~WindowsSessionEndWatch() {
    if (!thread_.joinable()) return;
    HWND window = nullptr;
    {
        const std::scoped_lock lock(state_->mutex);
        state_->released = true;
        window = state_->window;
    }
    state_->changed.notify_all();
    if (window != nullptr) PostMessageW(window, WM_CLOSE, 0, 0);
    thread_.join();
}

Result<void> WindowsSessionEndWatch::start(UniqueFunction<void()> on_end) {
    if (thread_.joinable()) return std::unexpected(call_failed("WindowsSessionEndWatch::start", ERROR_ALREADY_INITIALIZED));
    state_->on_end = std::move(on_end);
    // A start after a failed one must not see that attempt's outcome.
    state_->ready = false;
    state_->failure.reset();
    try {
        thread_ = std::thread([state = state_.get()] { run_window(*state); });
    } catch (...) {
        return std::unexpected(call_failed("CreateThread", ERROR_NOT_ENOUGH_MEMORY));
    }
    std::unique_lock lock(state_->mutex);
    state_->changed.wait(lock, [&] { return state_->ready; });
    if (!state_->failure) return {};
    Diagnostic failure = std::move(*state_->failure);
    lock.unlock();
    thread_.join();
    return std::unexpected(std::move(failure));
}

}  // namespace rb::os_windows::platform
