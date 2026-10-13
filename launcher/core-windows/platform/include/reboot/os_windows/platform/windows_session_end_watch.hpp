#pragma once

#include <chrono>
#include <memory>
#include <thread>

#include "reboot/base/diag.hpp"
#include "reboot/base/function.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_windows::platform {

// The hidden window's class, so a test can find the window and send it the session messages.
inline constexpr wchar_t kSessionEndWindowClass[] = L"RebootLauncherSessionEnd";

struct SessionEndState;

// Covers no capability ids; ISessionEndWatch over a hidden top-level window on its own thread
// (message-only windows get no session broadcast). WM_QUERYENDSESSION agrees at once; WM_ENDSESSION
// calls on_end and does not return until the watch is destroyed or `budget` has passed, since
// Windows terminates the process once it returns. The window is never visible, so Windows lists
// no shutdown block reason for it and ends the process 5 s into an unanswered WM_ENDSESSION.
class WindowsSessionEndWatch final : public ports::ISessionEndWatch {
public:
    // Answers before Windows' 5 s; the engine's signal poll plus its OsSignal shutdown fit inside.
    static constexpr std::chrono::milliseconds kDefaultBudget = ports::kSessionEndHold;

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
