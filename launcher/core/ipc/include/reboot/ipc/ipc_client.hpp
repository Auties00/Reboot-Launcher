#pragma once

#include <chrono>
#include <concepts>
#include <memory>
#include <span>
#include <string>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/ipc_client_sink.hpp"
#include "reboot/ipc/ipc_codec.hpp"

namespace rb {
class Executor;
class IClock;
}  // namespace rb

namespace rb::ports {
class IEngineStarter;
class IFileRevisionReader;
class IIpcConnector;
}  // namespace rb::ports

namespace rb::ipc {

enum class LaunchMode : u8 { Autostart, ConnectOnly };

struct IpcClientOptions {
    // From endpoint_for().
    std::string endpoint;
    NativePath engine_exe;
    DataRoot data_root;
    // canonical_root(data_root); every HelloAck must name the same root.
    NativePath canonical_root;
    // AppLayout::update_marker(); while it exists no engine is started.
    NativePath update_marker;
    // Sent as is on every connect, so whatever the facade puts in Hello reaches the engine.
    contracts::ipc::Hello hello;
    LaunchMode launch_mode = LaunchMode::Autostart;
    std::chrono::milliseconds connect_deadline = default_deadline(OpKind::EngineConnect);
};

struct IpcClientDeps {
    ports::IIpcConnector& connector;
    ports::IEngineStarter& starter;
    ports::IFileRevisionReader& files;
    IClock& clock;
    // Runs every connect round and backoff wait; a ManualExecutor in tests.
    Executor& executor;
    IIpcClientSink& sink;
};

// Covers no capability ids; the reboot_client end of the private IPC, one per rb_ctx. It owns
// connect, handshake, reconnect and frame writes; ClientContext owns replies, ops and events.
// After a loss Autostart runs connect rounds again with kReconnectBackoffMin..Max between them.
// Public members are thread-safe.
class IpcClient {
public:
    IpcClient(IpcClientDeps deps, IpcClientOptions options);
    // Closes, then waits for a sink call, `done` call, connect round or stream callback in progress.
    ~IpcClient();
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    // Calls `done` once on the executor, within connect_deadline of this call; a reconnect waiting
    // out its backoff starts at once. Each round checks the update marker, connects, and in
    // Autostart runs IEngineStarter::ensure_started when nobody listens.
    void connect(UniqueFunction<void(Result<Handshake>)> done);
    // Sends Goodbye{Normal} and stops reconnecting; no sink call starts after it returns.
    void close();

    // ipc.connection_lost while no link is up, ipc.message_too_large for a frame over
    // kIpcFrameCap. A frame sent just before a loss may or may not have reached the engine, so a
    // Start can have created its op without a Started.
    template <ContractMessage T>
        requires(!std::same_as<T, contracts::ipc::Hello> && !std::same_as<T, contracts::ipc::SecretPut>)
    Result<void> send(const T& message) {
        return write(IpcCodec::encode(message));
    }
    // The frame is wiped once written.
    Result<void> send_secret_put(std::span<const u8> target, const SecretBytes& secret);

private:
    Result<void> write(contracts::ipc::Bytes frame);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::ipc
