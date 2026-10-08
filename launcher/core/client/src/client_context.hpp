#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "call_failure.hpp"
#include "connect_settings.hpp"
#include "event_subscription.hpp"
#include "op_table.hpp"
#include "pending_calls.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/ipc_client_sink.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot {
class Executor;
class IClock;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class IPlatformPaths;
}  // namespace reboot::ports

namespace reboot::ipc {
class IpcClient;
}

namespace reboot::client {

// Borrowed; everything outlives the ClientContext.
struct ClientDeps {
    ports::IIpcConnector& connector;
    ports::IEngineStarter& starter;
    ports::ICallerContextProbe& caller;
    const ports::IPlatformPaths& paths;
    // IpcClient's update-marker check.
    ports::IFileSystem& files;
    IClock& clock;
    // Runs connect rounds, call deadlines and wakes; a ManualExecutor in tests.
    Executor& executor;
    // The calling user, whose engine endpoint is named after it.
    ports::PeerIdentity self;
    // REBOOT_LAUNCHER_HOME when set.
    std::optional<std::string> launcher_home;
};

struct Connected {
    // ipc.engine_image_differs, which rb_ctx_create leaves in rb_last_error.
    std::optional<Diagnostic> image_warning;
};

// Covers no capability ids. Everything behind one rb_ctx, over one ipc::IpcClient; thread-safe.
class ClientContext final : public ipc::IIpcClientSink {
public:
    // Data root from settings, then launcher_home, then the platform default; connect() follows.
    [[nodiscard]] static Result<std::unique_ptr<ClientContext>> create(ClientDeps deps, ConnectSettings settings);

    ClientContext(const ClientContext&) = delete;
    ClientContext& operator=(const ClientContext&) = delete;
    // Runs close(). The executor must not run this context's tasks any more.
    ~ClientContext() override;

    // `done` runs once. After the handshake the engine may take the foreground.
    void connect(UniqueFunction<void(Result<Connected>)> done);
    // Ends the link, fails waiting calls with client.closed and closes every subscription.
    void close();

    // A zero `timeout` defers to the engine's deadline; ipc.version_mismatch outside bootstrap.
    void call(u32 method, std::span<const u8> request, std::chrono::milliseconds timeout,
              UniqueFunction<void(CallResult<std::vector<u8>>)> done);
    // No `detached` value means the method's default disconnect policy.
    void start(u32 method, std::span<const u8> request, std::optional<bool> detached,
               UniqueFunction<void(CallResult<u64>)> done);
    void reveal_secret(std::span<const u8> target, UniqueFunction<void(CallResult<SecretBytes>)> done);

    [[nodiscard]] Result<void> attach(u64 op_id);
    // ipc.unknown_op unless the op is attached here.
    [[nodiscard]] Result<void> cancel(u64 op_id);
    [[nodiscard]] Result<OpState> op_state(u64 op_id) const;
    [[nodiscard]] Result<void> release(u64 op_id);

    // ipc.too_many_subscriptions past kMaxSubscriptions.
    [[nodiscard]] Result<u64> subscribe(std::span<const u8> filter);
    // Waits for a caller blocked on it and for a running wake.
    [[nodiscard]] Result<void> unsubscribe(u64 sub_id);
    // Credits what it takes; no `wait` waits forever; client.closed once closed and drained.
    [[nodiscard]] Result<std::optional<contracts::ipc::WireEvent>> next_event(
        u64 sub_id, std::optional<std::chrono::milliseconds> wait);
    // Posts the wake to the executor when events are already queued; an unknown sub is ignored.
    void set_wake(u64 sub_id, WakeCallback wake);

    [[nodiscard]] Result<void> put_secret(std::span<const u8> target, std::span<const u8> secret);
    void log_write(LogLevel level, std::string_view utf8);

    void on_frame(ipc::EngineMessage message) override;
    void on_lost(const Diagnostic& reason, ipc::LinkLoss loss) override;
    void on_reconnected(const ipc::Handshake& handshake) override;

private:
    explicit ClientContext(ClientDeps deps);

    [[nodiscard]] static contracts::ipc::CallerContext to_wire(const ports::CallerContext& caller);
    [[nodiscard]] Result<void> check_method(u32 method) const;
    // Opens a call and sends `frame`, failing the call if the write fails; no id when refused.
    template <class Frame>
    std::optional<u64> send_call(Frame frame, AnswerDone done);

    // Every subscription follows an op from the moment it is tracked.
    void track_op(u64 op_id, u32 method_id);
    void on_event_batch(contracts::ipc::EventBatch batch);
    void on_resync(u64 sub_id);
    // Records the outcome once and hands its OpCompleted to every following subscription.
    void deliver_outcome(u64 op_id, std::vector<u8> outcome);
    void fail_pending_ops(const Diagnostic& reason);
    void push_local_to_all(contracts::ipc::WireEvent event);
    void resubscribe_all();
    // Runs the wake and sends Credit for `effects`, with no lock held.
    void apply(u64 sub_id, const PushEffects& effects);
    void send_credit(u64 sub_id, u32 n);

    // Holds a use of the subscription so unsubscribe waits for the caller.
    [[nodiscard]] EventSubscription* acquire(u64 sub_id) const;

    ClientDeps deps_;
    std::unique_ptr<ipc::IpcClient> link_;

    mutable std::mutex link_mutex_;
    contracts::ipc::HelloAck hello_;
    // Counts handshakes, so events queued before a reconnect earn no credit.
    u64 link_generation_ = 0;
    std::optional<Diagnostic> lost_reason_;
    bool closed_ = false;

    PendingCalls calls_;
    OpTable ops_;

    // Before any subscription's lock; held while tracking ops so every subscription follows them.
    mutable std::mutex subs_mutex_;
    std::unordered_map<u64, std::unique_ptr<EventSubscription>> subs_;
    u64 next_sub_id_ = 1;
};

}  // namespace reboot::client
