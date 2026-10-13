#include "reboot/ipc/ipc_connection.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <variant>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/ipc/api_dispatcher.hpp"
#include "reboot/ipc/ipc_errors.hpp"
#include "reboot/ipc/ipc_limits.hpp"
#include "reboot/ipc/outbound_budget.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::ipc {

namespace wire = contracts::ipc;

namespace {

// A bound on what one WireEvent adds to its frame beyond the payload.
constexpr std::size_t kEventOverhead = 64;
// Keeps every EventBatch frame under kIpcFrameCap, which the client enforces.
constexpr std::size_t kMaxBatchBytes = kIpcFrameCap - 1024;

// Messages dropped unread may still hold a secret, also when the strand never runs their task.
struct Decoded {
    explicit Decoded(Result<std::vector<ClientMessage>> decoded) : messages(std::move(decoded)) {}
    Decoded(Decoded&&) noexcept = default;
    Decoded& operator=(Decoded&&) = delete;
    ~Decoded() {
        if (!messages) return;
        for (ClientMessage& message : *messages)
            if (auto* put = std::get_if<wire::SecretPut>(&message)) secure_wipe(put->bytes.data(), put->bytes.size());
    }

    Result<std::vector<ClientMessage>> messages;
};

// I/O thread: drops each Call or Start past the cap and answers it there, so a flood never
// queues more than kMaxOutstandingCalls of them on the strand.
void admit_requests(std::vector<ClientMessage>& messages, std::atomic<std::size_t>& in_flight,
                    ports::IByteStream& stream) {
    std::erase_if(messages, [&](const ClientMessage& message) {
        const auto* call = std::get_if<wire::Call>(&message);
        const auto* start = std::get_if<wire::Start>(&message);
        if (call == nullptr && start == nullptr) return false;
        if (in_flight.fetch_add(1) < wire::kMaxOutstandingCalls) return false;
        in_flight.fetch_sub(1);
        const Diagnostic refused = make_diag(ErrorDomain::Ipc, kTooManyCalls)
                                       .arg("limit", wire::kMaxOutstandingCalls)
                                       .kind(ErrorKind::Conflict)
                                       .retryable()
                                       .build();
        stream.write(IpcCodec::encode(wire::Reply{call != nullptr ? call->req_id : start->req_id, std::nullopt,
                                                  contracts::common::to_wire(refused)}));
        return true;
    });
}

}  // namespace

struct IpcConnection::Impl {
    struct Sub {
        std::shared_ptr<Subscription> subscription;
        u32 credit = kEventCreditWindow;
        // Budget bytes charged per event sent and not yet credited, oldest first.
        std::deque<std::size_t> charges;
        bool pump_posted = false;
    };

    Impl(ConnectionId connection, std::unique_ptr<ports::IByteStream> byte_stream, Executor& executor,
         const IClock& clock_ref, IApiDispatcher& dispatcher)
        : id(connection),
          stream(std::move(byte_stream)),
          strand(executor),
          api(dispatcher),
          peer(stream->peer()),
          budget(clock_ref) {}

    ConnectionId id;
    // Calls and Starts admitted on the I/O thread and not yet finished. Declared before `stream`,
    // whose destruction may wait for a read callback that counts here.
    std::atomic<std::size_t> in_flight{0};
    std::unique_ptr<ports::IByteStream> stream;
    Executor& strand;
    IApiDispatcher& api;
    ports::PeerIdentity peer;
    std::optional<ConnectionInfo> info;
    MessageHandler on_message;
    CloseHandler on_closed;
    std::map<SubscriptionId, Sub> subs;
    std::set<OpId> attached;
    OutboundBudget budget;
    bool closed = false;
    bool close_reported = false;
};

IpcConnection::IpcConnection(ConnectionId id, std::unique_ptr<ports::IByteStream> stream, Executor& strand,
                             const IClock& clock, IApiDispatcher& api)
    : impl_(std::make_unique<Impl>(id, std::move(stream), strand, clock, api)) {}

IpcConnection::~IpcConnection() {
    for (auto& [sub, entry] : impl_->subs) entry.subscription->set_notify({});
    impl_->stream->close();
}

void IpcConnection::start(MessageHandler on_message, CloseHandler on_closed) {
    impl_->on_message = std::move(on_message);
    impl_->on_closed = std::move(on_closed);
    const std::weak_ptr<IpcConnection> weak = weak_from_this();
    Executor& strand = impl_->strand;

    // I/O thread: decode and admit here, act on the strand. The stream outlives its own callbacks.
    impl_->stream->on_read([weak, &strand, stream = impl_->stream.get(), in_flight = &impl_->in_flight,
                            codec = std::make_unique<IpcCodec>()](std::span<const u8> bytes) mutable {
        Decoded decoded(codec->feed_from_client(bytes));
        if (decoded.messages) admit_requests(*decoded.messages, *in_flight, *stream);
        strand.post([weak, decoded = std::move(decoded)]() mutable {
            const std::shared_ptr<IpcConnection> self = weak.lock();
            if (!decoded.messages) {
                if (self && !self->impl_->closed) {
                    REBOOT_LOG_WARN(Ipc, "connection {} sent a bad frame: {}", self->impl_->id.value,
                                    decoded.messages.error().id);
                    self->close(wire::GoodbyeReason::ProtocolError);
                }
                return;
            }
            for (ClientMessage& message : *decoded.messages) {
                if (!self || self->impl_->closed) break;
                self->impl_->on_message(*self, std::move(message));
            }
        });
    });
    impl_->stream->on_close([weak, &strand] {
        strand.post([weak] {
            if (const std::shared_ptr<IpcConnection> self = weak.lock()) {
                self->impl_->closed = true;
                self->report_closed();
            }
        });
    });
}

ConnectionId IpcConnection::id() const noexcept { return impl_->id; }

const ports::PeerIdentity& IpcConnection::peer() const noexcept { return impl_->peer; }

void IpcConnection::greet(ConnectionInfo info) { impl_->info = std::move(info); }

bool IpcConnection::greeted() const noexcept { return impl_->info.has_value(); }

const ConnectionInfo& IpcConnection::info() const noexcept { return *impl_->info; }

void IpcConnection::send_frame(wire::Bytes frame) { write(std::move(frame)); }

void IpcConnection::reply_secret(u64 req_id, SecretBytes secret) {
    if (impl_->closed) return;
    const SecretBytes frame = IpcCodec::encode_secret_reply(req_id, secret);
    impl_->stream->write(frame.reveal());
}

void IpcConnection::finish_request() {
    if (impl_->in_flight.load() > 0) impl_->in_flight.fetch_sub(1);
}

Result<void> IpcConnection::add_subscription(SubscriptionId sub, std::shared_ptr<Subscription> subscription) {
    if (impl_->subs.contains(sub))
        return make_diag(ErrorDomain::Ipc, kDuplicateSubscription).arg("sub", sub.value).kind(ErrorKind::Conflict).fail();
    if (impl_->subs.size() >= wire::kMaxSubscriptions)
        return make_diag(ErrorDomain::Ipc, kTooManySubscriptions)
            .arg("limit", wire::kMaxSubscriptions)
            .kind(ErrorKind::Conflict)
            .fail();
    // The bus calls this inside publish; pumping later batches every event of the strand task.
    subscription->set_notify([weak = weak_from_this(), sub] {
        const std::shared_ptr<IpcConnection> self = weak.lock();
        if (!self) return;
        const auto it = self->impl_->subs.find(sub);
        if (it == self->impl_->subs.end() || it->second.pump_posted) return;
        it->second.pump_posted = true;
        self->impl_->strand.post([weak, sub] {
            if (const std::shared_ptr<IpcConnection> pumped = weak.lock()) pumped->pump(sub);
        });
    });
    Impl::Sub& entry = impl_->subs[sub];
    entry.subscription = std::move(subscription);
    pump(sub);
    return {};
}

Result<void> IpcConnection::remove_subscription(SubscriptionId sub) {
    const auto it = impl_->subs.find(sub);
    if (it == impl_->subs.end())
        return make_diag(ErrorDomain::Ipc, kUnknownSubscription).arg("sub", sub.value).kind(ErrorKind::NotFound).fail();
    for (const std::size_t charge : it->second.charges) impl_->budget.refund(charge);
    it->second.subscription->set_notify({});
    impl_->subs.erase(it);
    return {};
}

Result<void> IpcConnection::credit(SubscriptionId sub, u32 n) {
    const auto it = impl_->subs.find(sub);
    if (it == impl_->subs.end())
        return make_diag(ErrorDomain::Ipc, kUnknownSubscription).arg("sub", sub.value).kind(ErrorKind::NotFound).fail();
    Impl::Sub& entry = it->second;
    for (u32 i = 0; i < n && !entry.charges.empty(); ++i) {
        impl_->budget.refund(entry.charges.front());
        entry.charges.pop_front();
    }
    // A client never earns more than the window, whatever it sends.
    entry.credit = static_cast<u32>(std::min<u64>(u64{entry.credit} + n, kEventCreditWindow));
    pump(sub);
    return {};
}

void IpcConnection::pump(SubscriptionId sub) {
    const auto it = impl_->subs.find(sub);
    if (it == impl_->subs.end()) return;
    Impl::Sub& entry = it->second;
    entry.pump_posted = false;
    if (impl_->closed) return;
    if (entry.subscription->take_resync()) send(wire::Resync{sub.value});

    wire::EventBatch batch{sub.value, {}};
    std::size_t batch_bytes = 0;
    // Sends what is batched; false once pumping must stop.
    const auto flush = [&]() -> bool {
        if (batch.events.empty()) return true;
        wire::Bytes frame = IpcCodec::encode(batch);
        const std::size_t count = batch.events.size();
        batch.events.clear();
        batch_bytes = 0;
        if (!impl_->budget.fits(frame.size())) {
            if (impl_->budget.overflow() == OutboundBudget::Verdict::Disconnect) {
                REBOOT_LOG_WARN(Ipc, "connection {} is a slow consumer", impl_->id.value);
                close(wire::GoodbyeReason::SlowConsumer);
                return false;
            }
            // What is queued is dropped; the client re-fetches state after the Resync.
            std::vector<EventEnvelope> dropped;
            while (entry.subscription->drain(dropped, std::numeric_limits<std::size_t>::max()) > 0) dropped.clear();
            static_cast<void>(entry.subscription->take_resync());
            send(wire::Resync{sub.value});
            return false;
        }
        impl_->budget.charge(frame.size());
        for (std::size_t i = 0; i < count; ++i)
            entry.charges.push_back(frame.size() / count + (i == 0 ? frame.size() % count : 0));
        entry.credit -= static_cast<u32>(count);
        write(std::move(frame));
        return !impl_->closed;
    };

    bool oversized = false;
    std::vector<EventEnvelope> events;
    while (!impl_->closed && entry.credit > batch.events.size()) {
        events.clear();
        if (entry.subscription->drain(events, 1) == 0) break;
        std::optional<wire::WireEvent> encoded = impl_->api.encode_event(events.front(), impl_->id);
        if (!encoded) continue;
        const std::size_t size = encoded->payload.size() + kEventOverhead;
        if (size > kMaxBatchBytes) {
            oversized = true;
            continue;
        }
        if ((batch.events.size() == kMaxEventsPerBatch || batch_bytes + size > kMaxBatchBytes) && !flush()) return;
        batch.events.push_back(std::move(*encoded));
        batch_bytes += size;
    }
    if (!flush()) return;
    if (oversized) {
        REBOOT_LOG_WARN(Ipc, "connection {} lost an event too large for one frame", impl_->id.value);
        send(wire::Resync{sub.value});
    }
}

void IpcConnection::note_attached(OpId op) { impl_->attached.insert(op); }

void IpcConnection::note_released(OpId op) { impl_->attached.erase(op); }

bool IpcConnection::has_attached(OpId op) const noexcept { return impl_->attached.contains(op); }

std::vector<OpId> IpcConnection::attached_ops() const { return {impl_->attached.begin(), impl_->attached.end()}; }

void IpcConnection::close(wire::GoodbyeReason reason) {
    if (impl_->closed) return;
    write(IpcCodec::encode(wire::Goodbye{reason}));
    impl_->closed = true;
    impl_->stream->close();
    // Not every adapter reports a close it was asked for, so the handler is reached from here too.
    impl_->strand.post([weak = weak_from_this()] {
        if (const std::shared_ptr<IpcConnection> self = weak.lock()) self->report_closed();
    });
}

void IpcConnection::write(wire::Bytes frame) {
    if (impl_->closed) return;
    impl_->stream->write(frame);
}

void IpcConnection::report_closed() {
    if (impl_->close_reported) return;
    impl_->close_reported = true;
    if (CloseHandler handler = std::move(impl_->on_closed)) handler(*this);
}

}  // namespace rb::ipc
