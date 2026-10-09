#pragma once

namespace rb::os_windows::winhost {

// reboot-winhost.exe's process exit code, which the runner reports to the engine.
enum class WinhostExit : int {
    // The engine closed the channel, or it failed, after Welcome; the Job was terminated.
    EngineClosed = 0,
    // A Stop emptied the Job and winhost disconnected.
    Stopped = 1,
    BadBootstrap = 2,
    ConnectFailed = 3,
    // EOF before Welcome: the engine refused the Hello.
    Refused = 4,
    LaunchFailed = 5,
    ProtocolError = 6,
    InternalError = 7,
};

}  // namespace rb::os_windows::winhost
