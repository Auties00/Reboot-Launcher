#pragma once

#include <memory>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/connection_info.hpp"
#include "reboot/ipc/ipc_codec.hpp"

namespace rb {
class Executor;
class IClock;
}  // namespace rb

namespace rb::ports {
class IByteStream;
}

namespace rb::ipc {

class IApiDispatcher;

// Covers no capability ids. One accepted client of IpcServer; strand-only. The stream's I/O
// callbacks hold only a weak_ptr and post decoded messages to the strand.
class IpcConnection : public std::enable_shared_from_this<IpcConnection> {
public:
    using MessageHandler = UniqueFunction<void(IpcConnection&, ClientMessage)>;
    using CloseHandler = UniqueFunction<void(IpcConnection&)>;

    IpcConnection(ConnectionId id, std::unique_ptr<ports::IByteStream> stream, Executor& strand,
                  const IClock& clock, IApiDispatcher& api);
    ~IpcConnection();
    IpcConnection(const IpcConnection&) = delete;
    IpcConnection& operator=(const IpcConnection&) = delete;

    // A Call or Start over kMaxOutstandingCalls is answered with ipc.too_many_calls here.
    // `on_closed` runs once, after the last message.
    void start(MessageHandler on_message, CloseHandler on_closed);

    [[nodiscard]] ConnectionId id() const noexcept;
    [[nodiscard]] const ports::PeerIdentity& peer() const noexcept;

    void greet(ConnectionInfo info);
    [[nodiscard]] bool greeted() const noexcept;
    // Valid once greeted.
    [[nodiscard]] const ConnectionInfo& info() const noexcept;

    template <ContractMessage T>
    void send(const T& message) {
        write(IpcCodec::encode(message));
    }
    // A frame IpcCodec::encode produced.
    void send_frame(contracts::ipc::Bytes frame);
    // The Reply to a SecretReveal; its frame is wiped once written.
    void reply_secret(u64 req_id, SecretBytes secret);
    // Ends one outstanding Call or Start.
    void finish_request();

    // ipc.too_many_subscriptions or ipc.duplicate_subscription.
    Result<void> add_subscription(SubscriptionId sub, std::shared_ptr<Subscription> subscription);
    Result<void> remove_subscription(SubscriptionId sub);
    Result<void> credit(SubscriptionId sub, u32 n);
    // Sends a pending Resync, then EventBatches while credit and the OutboundBudget allow.
    void pump(SubscriptionId sub);

    // Ops started or attached here and not released; they receive OpResult.
    void note_attached(OpId op);
    void note_released(OpId op);
    [[nodiscard]] bool has_attached(OpId op) const noexcept;
    [[nodiscard]] std::vector<OpId> attached_ops() const;

    // Sends Goodbye{reason} and closes the stream; idempotent.
    void close(contracts::ipc::GoodbyeReason reason);

private:
    void deliver(ClientMessage message);
    void write(contracts::ipc::Bytes frame);
    void report_closed();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::ipc
