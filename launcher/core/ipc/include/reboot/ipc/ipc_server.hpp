#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class EventBus;
class Executor;
class IClock;
class OpRegistry;
class TimerService;
}  // namespace reboot

namespace reboot::ports {
class IIpcListener;
}

namespace reboot::ipc {

class IApiDispatcher;

// HelloAck minus the per-connection compatibility.
struct EngineHello {
    std::string engine_build;
    EngineEpoch epoch;
    u32 pid = 0;
    NativePath image_path;
    NativePath canonical_root;
    contracts::ipc::EngineOrigin origin{};
    contracts::ipc::StorageMode storage_mode{};
    bool secrets_available = false;
};

struct IpcServerDeps {
    ports::IIpcListener& listener;
    Executor& strand;
    const IClock& clock;
    TimerService& timers;
    OpRegistry& ops;
    EventBus& events;
    IApiDispatcher& api;
};

// Covers no capability ids; the engine end of the private IPC. Strand-only.
// A Subscribe the engine cannot register is logged and dropped; the connection stays up.
// Subscribe, SecretPut and SecretReveal carry reboot.api.v1 bytes, so BootstrapOnly gets
// ipc.version_mismatch for them. A Reply or OpResult over kIpcFrameCap becomes ipc.message_too_large.
class IpcServer {
public:
    IpcServer(IpcServerDeps deps, EngineHello hello);
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    // Called only while state/engine.lock is held, so the listener may replace a stale endpoint.
    Result<void> start(std::string_view endpoint);
    // Stops accepting, then sends Goodbye{reason} to every connection and closes it.
    void stop(contracts::ipc::GoodbyeReason reason);

    // On a change, sends every greeted connection a fresh HelloAck.
    void set_secrets_available(bool available);

    // The API carries no ForegroundHint event, so the engine forwards EventKind::ForegroundHint here.
    void send_foreground_hint(u32 pid);

    [[nodiscard]] std::size_t connection_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::ipc
