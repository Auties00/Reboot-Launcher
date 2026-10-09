#pragma once

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ports {

// `user_id` is the SID string on Windows and the decimal uid on POSIX.
struct PeerIdentity {
    std::string user_id;
    u32 pid = 0;
};

// Callbacks run on the I/O thread. write() is thread-safe.
class IByteStream {
public:
    virtual ~IByteStream() = default;

    virtual void write(std::span<const u8> bytes) = 0;
    virtual void on_read(UniqueFunction<void(std::span<const u8>)> callback) = 0;
    virtual void on_close(UniqueFunction<void()> callback) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual PeerIdentity peer() const = 0;
};

// Hands over a stream only after the peer's identity matched the engine's user.
class IIpcListener {
public:
    virtual ~IIpcListener() = default;

    virtual Result<void> listen(std::string_view endpoint_name,
                                UniqueFunction<void(std::unique_ptr<IByteStream>)> on_accept) = 0;
    virtual void close() = 0;
};

// Verifies the endpoint's owner before returning; a mismatch fails with ipc.endpoint_untrusted.
class IIpcConnector {
public:
    virtual ~IIpcConnector() = default;

    virtual Result<std::unique_ptr<IByteStream>> connect(std::string_view endpoint_name,
                                                         std::chrono::milliseconds deadline) = 0;
};

enum class StartResult : u8 {
    Started,
    AlreadyRunning,
    AwaitingUser,
    CannotDetach,
    ElevatedRefused,
    NoInteractiveSession,
    ConnectOnly,
};

// Runs under state/spawn.lock, so racing clients start one engine.
class IEngineStarter {
public:
    virtual ~IEngineStarter() = default;

    virtual Result<StartResult> ensure_started(const NativePath& engine_exe, const DataRoot& root) = 0;
};

struct CallerContext {
    std::string os_session;
    bool elevated = false;
    bool interactive = false;
    std::vector<std::pair<std::string, std::string>> display_env;
};

class ICallerContextProbe {
public:
    virtual ~ICallerContextProbe() = default;

    [[nodiscard]] virtual CallerContext capture() const = 0;
    // AllowSetForegroundWindow on Windows; nothing elsewhere.
    virtual void allow_foreground(u32 pid) = 0;
};

// Defined by each OS module: the named pipe name on Windows, the socket path on POSIX.
[[nodiscard]] std::string endpoint_name(const PeerIdentity& self, std::string_view root_hash16);

}  // namespace rb::ports
