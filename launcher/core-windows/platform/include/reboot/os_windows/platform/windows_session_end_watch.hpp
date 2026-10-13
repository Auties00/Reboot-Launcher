#pragma once

#include <chrono>
#include <memory>
#include <thread>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_windows::platform {

// The hidden window's class, so a test can find the window and send it the session messages.
inline constexpr wchar_t kSessionEndWindowClass[] = L"RebootLauncherSessionEnd";

struct SessionEndState;

// Covers no capability ids; ISessionEndWatch over a hidden top-level window on its own thread
// (message-only windows get no session broadcast). WM_QUERYENDSESSION registers a shutdown block
// reason and agrees; WM_ENDSESSION calls on_end and does not return until the watch is destroyed
// or `budget` has passed, since Windows terminates the process once it returns.
class WindowsSessionEndWatch final : public ports::ISessionEndWatch {
public:
    // An OsSignal shutdown fits in 5 s; the rest covers the signal poll, teardown and log flush.
    static constexpr std::chrono::milliseconds kDefaultBudget{8000};

    explicit WindowsSessionEndWatch(std::chrono::milliseconds budget = kDefaultBudget);
    ~WindowsSessionEndWatch() override;
    WindowsSessionEndWatch(const WindowsSessionEndWatch&) = delete;
    WindowsSessionEndWatch& operator=(const WindowsSessionEndWatch&) = delete;

    // Creates the window; a second call fails.
    Result<void> start(UniqueFunction<void()> on_end) override;

private:
    std::unique_ptr<SessionEndState> state_;
    std::thread thread_;
};

}  // namespace rb::os_windows::platform
