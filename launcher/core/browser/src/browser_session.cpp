#include "reboot/browser/browser_session.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "messages.hpp"
#include "reboot/browser/full_jitter_backoff.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/net/address_resolver.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/ports/net.hpp"
#include "wire/frame.hpp"

namespace reboot::browser {

namespace {

namespace wire = sb::wire;

// Answers on the control stream; a QueryResult page is the largest.
constexpr std::size_t kControlFrameCap = std::size_t{1} << 20;
// A snapshot is one window of at most 200 entries.
constexpr std::size_t kStreamFrameCap = std::size_t{4} << 20;
constexpr std::size_t kDatagramFrameCap = std::size_t{64} << 10;
// Snapshots and deltas can overtake their SubOpen; these bound what waits for it.
constexpr std::size_t kOrphanViews = 16;
constexpr std::size_t kOrphanFrames = 256;
constexpr std::chrono::seconds kProbeTimeout{5};
constexpr std::size_t kProbeMaxBody = 4096;
// Room for a Join frame's fixed fields, so encoding the password never reallocates.
constexpr std::size_t kJoinFrameReserve = 96;

constexpr std::string_view kUdpBlockedId = "net.quic_udp_blocked";
constexpr std::string_view kHostNotFoundId = "net.host_not_found";

// Every answer to a request carries its req_id as field 1.
struct AnswerHeader {
    u32 req_id = 0;
};

[[nodiscard]] std::string dial_text(const RbsbEndpoint& endpoint) {
    const bool ipv6_literal = endpoint.host.find(':') != std::string::npos;
    return (ipv6_literal ? '[' + endpoint.host + ']' : endpoint.host) + ':' + std::to_string(endpoint.port.value);
}

void wipe(std::string& text) noexcept {
    text.resize(text.capacity());
    secure_wipe(text.data(), text.size());
    text.clear();
}

struct Reply {
    std::span<const u8> payload;
    std::optional<RbsbRequestError> error;
};

template <class T>
[[nodiscard]] UniqueFunction<void(Reply)> completer(UniqueFunction<void(RbsbResult<T>)> done) {
    return [done = std::move(done)](Reply reply) mutable {
        if (reply.error) {
            done(std::unexpected(std::move(*reply.error)));
            return;
        }
        T message;
        if (!wire::decode(reply.payload, message)) {
            done(std::unexpected(
                RbsbRequestError{RbsbFailure::Rejected, wire::ErrorCode::internal, "malformed answer", {}, std::nullopt}));
            return;
        }
        done(std::move(message));
    };
}

[[nodiscard]] RbsbRequestError failure(RbsbFailure kind) {
    return RbsbRequestError{kind, wire::ErrorCode::unknown, {}, {}, std::nullopt};
}

}  // namespace

struct BrowserSession::Impl {
    struct Alive {
        Impl* impl = nullptr;
    };

    enum class Phase : u8 { Idle, Resolving, Handshake, AwaitWelcome, Probing, Waiting, Connected, Draining };
    enum class ProbeStage : u8 { Status, Connectivity };

    struct Pending {
        wire::FrameType answer{};
        UniqueFunction<std::vector<u8>(u32 req_id)> encode;
        UniqueFunction<void(Reply)> complete;
        // 0 until sent.
        u32 req_id = 0;
        u64 lease = 0;
        TimerHandle timer;
        CancelRegistration cancel;
    };

    struct Sub {
        wire::ViewSpec view;
        u32 window = 0;
        // Shared, so a callback that unsubscribes its own view does not destroy itself.
        std::shared_ptr<ViewStreamCallbacks> callbacks;
        u64 lease = 0;
        // 0 while not subscribed on the current connection.
        u32 sub_id = 0;
        u32 req_id = 0;
        std::optional<u32> view_id;
        bool rejected = false;
    };

    using OrphanFrame = std::variant<wire::Snapshot, wire::Delta>;

    // What one dropped connection leaves to be told, after the session's own state is settled.
    struct Fallout {
        std::vector<u64> lost_views;
        std::vector<u64> lost_requests;
    };

    Impl(BrowserSessionDeps deps_in, RbsbEndpoint endpoint_in, BrowserSessionOptions options_in)
        : deps(deps_in), endpoint(std::move(endpoint_in)), options(std::move(options_in)),
          alive(std::make_shared<Alive>(Alive{this})), backoff(deps_in.random) {}

    ~Impl() {
        alive->impl = nullptr;
        if (attempt) attempt->cancel(CancelReason::Shutdown);
        if (connection) connection->close(0);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // ---- status ----------------------------------------------------------------------------

    void set_status(ConnectionState state, std::optional<SteadyTime> next_attempt, std::optional<Diagnostic> error) {
        const bool same = status.state == state && status.next_attempt == next_attempt &&
                          status.error.has_value() == error.has_value() && (!error || status.error->id == error->id);
        status = ConnectionStatus{state, next_attempt, std::move(error)};
        if (!same) deps.events.publish(EventKind::BrowserConnectionChanged, status);
    }

    [[nodiscard]] bool serving() const noexcept { return phase == Phase::Connected || phase == Phase::Draining; }

    [[nodiscard]] u32 next_request_id() noexcept {
        if (next_req == 0) next_req = 1;
        return next_req++;
    }

    // ---- leases ----------------------------------------------------------------------------

    u64 acquire_lease() {
        const u64 id = next_id++;
        leases.insert(id);
        if (phase == Phase::Idle) start_attempt();
        return id;
    }

    void release_lease(u64 id) {
        if (leases.erase(id) == 0 || !leases.empty()) return;
        go_idle();
    }

    void go_idle() {
        cancel_attempt();
        retry_timer.cancel();
        connect_timer.cancel();
        drain_at.reset();
        Fallout fallout = detach_connection();
        phase = Phase::Idle;
        backoff.reset();
        set_status(ConnectionState::Idle, std::nullopt, std::nullopt);
        notify(std::move(fallout));
    }

    // ---- connecting ------------------------------------------------------------------------

    void cancel_attempt() {
        if (attempt) attempt->cancel(CancelReason::Superseded);
        attempt.reset();
        ++epoch;
    }

    void start_attempt() {
        cancel_attempt();
        attempt = std::make_unique<CancelSource>();
        evidence = {};
        last_failure.reset();
        candidates.clear();
        candidate = 0;
        phase = Phase::Resolving;
        set_status(ConnectionState::Connecting, std::nullopt, status.error);

        const u64 gen = epoch;
        Result<void> started = deps.resolver.resolve(
            dial_text(endpoint), endpoint.port, net::AddressFamilyPolicy::Any, attempt->token(),
            [weak = alive, gen](Result<net::ResolvedAddress> resolved) mutable {
                if (Impl* self = weak->impl) self->on_resolved(gen, std::move(resolved));
            });
        if (!started) {
            last_failure = std::move(started.error());
            schedule_retry(ConnectionState::Backoff, edge_unreachable());
        }
    }

    void on_resolved(u64 gen, Result<net::ResolvedAddress> resolved) {
        if (gen != epoch || phase != Phase::Resolving) return;
        if (!resolved) {
            evidence.dns_failed = resolved.error().id == kHostNotFoundId;
            last_failure = std::move(resolved.error());
            probe();
            return;
        }
        candidates = std::move(resolved->endpoints);
        candidate = 0;
        try_candidate();
    }

    void try_candidate() {
        while (candidate < candidates.size()) {
            const u64 gen = ++epoch;
            ports::QuicConnectOptions connect;
            connect.host = endpoint.host;
            connect.port = endpoint.port;
            connect.alpn = std::string(wire::kAlpn);
            connect.ca_bundle = endpoint.ca_bundle;
            connect.remote = candidates[candidate].address;
            connect.keepalive = options.keepalive;
            Result<std::unique_ptr<ports::IQuicConnection>> opened =
                deps.quic.open_connection(connect, quic_callbacks(gen));
            if (!opened) {
                last_failure = std::move(opened.error());
                ++candidate;
                continue;
            }
            connection = std::move(*opened);
            phase = Phase::Handshake;
            connect_timer = deps.timers.after(options.connect_timeout, [weak = alive, gen] {
                if (Impl* self = weak->impl) self->on_connect_timeout(gen);
            });
            return;
        }
        probe();
    }

    [[nodiscard]] ports::QuicCallbacks quic_callbacks(u64 gen) {
        // MsQuic calls these on its own threads; everything is copied and posted to the strand.
        Executor* strand = &deps.strand;
        ports::QuicCallbacks callbacks;
        callbacks.on_connected = [weak = alive, strand, gen] {
            strand->post([weak, gen] {
                if (Impl* self = weak->impl) self->on_connected(gen);
            });
        };
        callbacks.on_stream_data = [weak = alive, strand, gen](u64 stream, std::span<const u8> data, bool fin) {
            strand->post([weak, gen, stream, bytes = std::vector<u8>(data.begin(), data.end()), fin]() mutable {
                if (Impl* self = weak->impl) self->on_stream_data(gen, stream, std::move(bytes), fin);
            });
        };
        callbacks.on_datagram = [weak = alive, strand, gen](std::span<const u8> data) {
            strand->post([weak, gen, bytes = std::vector<u8>(data.begin(), data.end())]() mutable {
                if (Impl* self = weak->impl) self->on_datagram(gen, std::move(bytes));
            });
        };
        callbacks.on_closed = [weak = alive, strand, gen](std::optional<Diagnostic> error) {
            strand->post([weak, gen, error = std::move(error)]() mutable {
                if (Impl* self = weak->impl) self->on_closed(gen, std::move(error));
            });
        };
        return callbacks;
    }

    void on_connected(u64 gen) {
        if (gen != epoch || phase != Phase::Handshake) return;
        Result<u64> stream = connection->open_stream();
        if (!stream) {
            candidate_failed(std::move(stream.error()), false);
            return;
        }
        control = *stream;
        control_framer.emplace(kControlFrameCap);
        phase = Phase::AwaitWelcome;
        const wire::Hello hello{wire::Role::browser, wire::kProtoMinor, options.client_version, wire::feature::datagrams};
        if (Result<void> sent = connection->send(*control, wire::frame_bytes(hello), false); !sent)
            candidate_failed(std::move(sent.error()), false);
    }

    void on_connect_timeout(u64 gen) {
        if (gen != epoch || (phase != Phase::Handshake && phase != Phase::AwaitWelcome)) return;
        candidate_failed(edge_unreachable(), true);
    }

    void candidate_failed(Diagnostic error, bool timed_out) {
        connect_timer.cancel();
        if (timed_out || error.id == kUdpBlockedId) evidence.quic_timed_out = true;
        last_failure = std::move(error);
        close_connection();
        ++candidate;
        try_candidate();
    }

    void on_welcome(const wire::Welcome& welcome) {
        connect_timer.cancel();
        cancel_attempt_keep_epoch();
        phase = Phase::Connected;
        backoff.reset();
        candidates.clear();
        const auto local_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(deps.clock.system_now().time_since_epoch());
        const std::chrono::milliseconds edge_ms{static_cast<std::chrono::milliseconds::rep>(welcome.server_time_ms)};
        edge = EdgeSession{welcome.edge_id, welcome.features, edge_ms - local_ms, welcome.limits};
        set_status(ConnectionState::Connected, std::nullopt, std::nullopt);

        const u64 gen = epoch;
        std::vector<u64> views;
        for (const auto& [id, sub] : subs)
            if (!sub.rejected) views.push_back(id);
        for (const u64 id : views)
            if (gen == epoch && serving()) send_subscribe(id);
        std::vector<u64> waiting;
        for (const auto& [id, request] : pending)
            if (request.req_id == 0) waiting.push_back(id);
        for (const u64 id : waiting)
            if (gen == epoch && serving()) send_request(id);
    }

    // The attempt's lookups and probes are over; callbacks of the live connection keep the epoch.
    void cancel_attempt_keep_epoch() {
        if (attempt) attempt->cancel(CancelReason::Superseded);
        attempt.reset();
    }

    // ---- explaining a failed attempt -------------------------------------------------------

    void probe() {
        phase = Phase::Probing;
        if (evidence.dns_failed) {
            probe_connectivity();
            return;
        }
        run_probe(endpoint.status_url(), ProbeStage::Status);
    }

    void probe_connectivity() {
        if (options.connectivity_url.empty()) {
            classify();
            return;
        }
        run_probe(options.connectivity_url, ProbeStage::Connectivity);
    }

    void run_probe(std::string url, ProbeStage stage) {
        net::HttpRequest request;
        request.method = net::HttpMethod::Get;
        request.url = std::move(url);
        request.limits = net::HttpLimits{kProbeTimeout, kProbeTimeout, std::nullopt};
        request.retry = net::kNoRetry;
        request.max_body = kProbeMaxBody;
        const u64 gen = epoch;
        Result<void> started = deps.http.send(std::move(request), attempt ? attempt->token() : CancelToken{},
                                              [weak = alive, gen, stage](Result<net::HttpResponse> response) {
                                                  if (Impl* self = weak->impl) self->on_probe(gen, stage, response);
                                              });
        if (!started) on_probe(gen, stage, std::unexpected(std::move(started.error())));
    }

    void on_probe(u64 gen, ProbeStage stage, const Result<net::HttpResponse>& response) {
        if (gen != epoch || phase != Phase::Probing) return;
        HttpsProbe verdict = HttpsProbe::Failed;
        if (response) verdict = response->status >= 200 && response->status < 300 ? HttpsProbe::Ok : HttpsProbe::Unavailable;
        if (stage == ProbeStage::Connectivity) {
            evidence.connectivity = verdict;
            classify();
            return;
        }
        evidence.status = verdict;
        if (verdict == HttpsProbe::Failed) probe_connectivity();
        else classify();
    }

    void classify() {
        const ConnectionState state = classify_connect_failure(evidence);
        const auto host_diag = [&](MessageId id) {
            return make_diag(ErrorDomain::Browser, id).arg("host", endpoint.host).retryable().build();
        };
        switch (state) {
            case ConnectionState::Offline: {
                DiagBuilder builder = make_diag(ErrorDomain::Browser, kOffline).retryable();
                if (last_failure) std::move(builder).cause(*last_failure);
                schedule_retry(state, std::move(builder).build());
                return;
            }
            case ConnectionState::ServiceDown: schedule_retry(state, host_diag(kServiceDown)); return;
            case ConnectionState::UdpBlocked: schedule_retry(state, host_diag(kUdpBlocked)); return;
            default: schedule_retry(ConnectionState::Backoff, edge_unreachable()); return;
        }
    }

    [[nodiscard]] Diagnostic edge_unreachable() const {
        DiagBuilder builder = make_diag(ErrorDomain::Browser, kEdgeUnreachable).arg("host", endpoint.host).retryable();
        if (last_failure) std::move(builder).cause(*last_failure);
        return std::move(builder).build();
    }

    void schedule_retry(ConnectionState state, Diagnostic error) {
        cancel_attempt_keep_epoch();
        phase = Phase::Waiting;
        const SteadyTime at = deps.clock.steady_now() + backoff.next();
        retry_timer = deps.timers.at(at, [weak = alive] {
            if (Impl* self = weak->impl) self->on_retry();
        });
        set_status(state, at, std::move(error));
    }

    void on_retry() {
        drain_at.reset();
        if (phase == Phase::Waiting && !leases.empty()) start_attempt();
    }

    // ---- the live connection ---------------------------------------------------------------

    void close_connection() {
        if (connection) connection->close(0);
        connection.reset();
        control.reset();
        control_framer.reset();
        streams.clear();
        ++epoch;
    }

    // Leaves the session with no connection; the caller settles phase and status, then notifies.
    Fallout detach_connection() {
        Fallout fallout;
        const bool was_serving = serving();
        close_connection();
        edge.reset();
        orphans.clear();
        orphan_order.clear();
        by_req.clear();
        sub_by_req.clear();
        sub_by_wire.clear();
        for (auto& [id, sub] : subs) {
            if (sub.sub_id == 0) continue;
            sub.sub_id = 0;
            sub.req_id = 0;
            sub.view_id.reset();
            if (was_serving) fallout.lost_views.push_back(id);
        }
        for (const auto& [id, request] : pending)
            if (request.req_id != 0) fallout.lost_requests.push_back(id);
        return fallout;
    }

    void notify(Fallout fallout) {
        for (const u64 id : fallout.lost_requests) complete_pending(id, Reply{{}, failure(RbsbFailure::ConnectionLost)});
        for (const u64 id : fallout.lost_views) {
            const auto it = subs.find(id);
            if (it == subs.end()) continue;
            const std::shared_ptr<ViewStreamCallbacks> callbacks = it->second.callbacks;
            if (callbacks->on_lost) callbacks->on_lost();
        }
    }

    void on_closed(u64 gen, std::optional<Diagnostic> error) {
        if (gen != epoch) return;
        if (phase == Phase::Handshake || phase == Phase::AwaitWelcome) {
            const bool timed_out = error && error->id == kUdpBlockedId;
            candidate_failed(error ? std::move(*error) : edge_unreachable(), timed_out);
            return;
        }
        if (serving()) connection_lost(std::move(error));
    }

    void connection_lost(std::optional<Diagnostic> error) {
        const bool draining = phase == Phase::Draining && drain_at;
        Fallout fallout = detach_connection();
        if (draining) {
            // The GoAway delay already decides when to come back.
            phase = Phase::Waiting;
            set_status(ConnectionState::Backoff, drain_at, std::nullopt);
        } else {
            DiagBuilder builder = make_diag(ErrorDomain::Browser, kConnectionLost).retryable();
            if (error) std::move(builder).cause(std::move(*error));
            schedule_retry(ConnectionState::Backoff, std::move(builder).build());
        }
        notify(std::move(fallout));
    }

    void protocol_violation() {
        const Diagnostic error = make_diag(ErrorDomain::Browser, kConnectionLost).retryable().build();
        if (serving()) connection_lost(error);
        else if (phase == Phase::AwaitWelcome) candidate_failed(error, false);
    }

    void on_stream_data(u64 gen, u64 stream, std::vector<u8> bytes, bool fin) {
        if (gen != epoch) return;
        std::vector<std::pair<wire::FrameType, std::vector<u8>>> frames;
        const auto collect = [&](const wire::FrameView& frame) {
            frames.emplace_back(frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end()));
            return true;
        };
        if (control && stream == *control) {
            if (control_framer->feed(bytes, collect) != wire::StreamFramer::Status::ok) {
                protocol_violation();
                return;
            }
        } else {
            auto it = streams.find(stream);
            if (it == streams.end()) it = streams.emplace(stream, wire::StreamFramer(kStreamFrameCap)).first;
            // A broken stream stays known until its FIN, so its later bytes are never framed afresh.
            if (it->second && it->second->feed(bytes, collect) != wire::StreamFramer::Status::ok) it->second.reset();
            if (fin) streams.erase(it);
        }
        // A frame's callbacks may drop the connection; the rest of its frames then go with it.
        for (auto& [type, payload] : frames) {
            if (gen != epoch) return;
            handle_frame(type, payload);
        }
    }

    void on_datagram(u64 gen, std::vector<u8> bytes) {
        if (gen != epoch) return;
        std::vector<std::vector<u8>> deltas;
        (void)wire::for_each_frame(bytes, kDatagramFrameCap, [&](const wire::FrameView& frame) {
            if (frame.type == wire::FrameType::delta) deltas.emplace_back(frame.payload.begin(), frame.payload.end());
            return true;
        });
        for (const auto& payload : deltas) {
            if (gen != epoch) return;
            handle_frame(wire::FrameType::delta, payload);
        }
    }

    void handle_frame(wire::FrameType type, std::span<const u8> payload) {
        if (phase == Phase::AwaitWelcome) {
            wire::Welcome welcome;
            if (type == wire::FrameType::welcome && wire::decode(payload, welcome)) on_welcome(welcome);
            return;
        }
        if (!serving()) return;
        switch (type) {
            case wire::FrameType::sub_open: {
                wire::SubOpen open;
                if (wire::decode(payload, open)) on_sub_open(open);
                return;
            }
            case wire::FrameType::query_result:
            case wire::FrameType::resolve_result:
            case wire::FrameType::join_grant: on_answer(type, payload); return;
            case wire::FrameType::error: {
                wire::Error error;
                if (wire::decode(payload, error)) on_error(error);
                return;
            }
            case wire::FrameType::go_away: {
                wire::GoAway go_away;
                if (wire::decode(payload, go_away)) on_go_away(go_away);
                return;
            }
            case wire::FrameType::snapshot: {
                wire::Snapshot snapshot;
                if (wire::decode(payload, snapshot)) on_view_frame(snapshot.view_id, OrphanFrame(std::move(snapshot)));
                return;
            }
            case wire::FrameType::delta: {
                wire::Delta delta;
                if (wire::decode(payload, delta)) on_view_frame(delta.view_id, OrphanFrame(std::move(delta)));
                return;
            }
            default: return;
        }
    }

    void on_go_away(const wire::GoAway& go_away) {
        if (phase != Phase::Connected) return;
        phase = Phase::Draining;
        drain_at = deps.clock.steady_now() +
                   go_away_delay(deps.random, std::chrono::milliseconds{go_away.reconnect_after_ms});
        retry_timer = deps.timers.at(*drain_at, [weak = alive] {
            if (Impl* self = weak->impl) self->on_drain_end();
        });
        set_status(ConnectionState::Draining, drain_at, std::nullopt);
    }

    void on_drain_end() {
        drain_at.reset();
        if (leases.empty()) return;
        if (phase == Phase::Waiting) {
            start_attempt();
            return;
        }
        if (phase != Phase::Draining) return;
        Fallout fallout = detach_connection();
        start_attempt();
        notify(std::move(fallout));
    }

    // ---- requests --------------------------------------------------------------------------

    void issue(wire::FrameType answer, UniqueFunction<std::vector<u8>(u32)> encode, UniqueFunction<void(Reply)> complete,
               const CancelToken& token) {
        const u64 id = next_id++;
        Pending& request = pending[id];
        request.answer = answer;
        request.encode = std::move(encode);
        request.complete = std::move(complete);
        request.lease = acquire_lease();
        Executor* strand = &deps.strand;
        request.cancel = token.on_cancel([weak = alive, strand, id](CancelReason) {
            strand->post([weak, id] {
                if (Impl* self = weak->impl) self->complete_pending(id, Reply{{}, failure(RbsbFailure::Cancelled)});
            });
        });
        const auto it = pending.find(id);
        // An already cancelled token has posted its completion; nothing goes on the wire.
        if (it == pending.end() || it->second.req_id != 0 || token.cancelled()) return;
        if (serving()) {
            send_request(id);
            return;
        }
        it->second.timer = deps.timers.after(options.request_wait, [weak = alive, id] {
            if (Impl* self = weak->impl) self->on_wait_expired(id);
        });
    }

    void send_request(u64 id) {
        Pending& request = pending.at(id);
        request.timer.cancel();
        request.req_id = next_request_id();
        by_req[request.req_id] = id;
        std::vector<u8> bytes = request.encode(request.req_id);
        request.encode = nullptr;
        if (Result<void> sent = connection->send(*control, std::move(bytes), false); !sent) {
            // Posted, so `done` never runs inside the call that issued the request.
            deps.strand.post([weak = alive, id] {
                if (Impl* self = weak->impl) self->complete_pending(id, Reply{{}, failure(RbsbFailure::ConnectionLost)});
            });
            return;
        }
        request.timer = deps.timers.after(options.request_timeout, [weak = alive, id] {
            if (Impl* self = weak->impl) self->complete_pending(id, Reply{{}, failure(RbsbFailure::TimedOut)});
        });
    }

    void on_wait_expired(u64 id) {
        const auto it = pending.find(id);
        if (it == pending.end() || it->second.req_id != 0) return;
        RbsbRequestError error = failure(RbsbFailure::NotConnected);
        error.cause = status.error;
        complete_pending(id, Reply{{}, std::move(error)});
    }

    void complete_pending(u64 id, Reply reply) {
        const auto it = pending.find(id);
        if (it == pending.end()) return;
        Pending request = std::move(it->second);
        pending.erase(it);
        if (request.req_id != 0) by_req.erase(request.req_id);
        request.timer.cancel();
        request.cancel.reset();
        request.complete(std::move(reply));
        // Released after `done`, so a follow-up request keeps the connection open.
        release_lease(request.lease);
    }

    void on_answer(wire::FrameType type, std::span<const u8> payload) {
        AnswerHeader header;
        if (!wire::decode(payload, header)) return;
        const auto it = by_req.find(header.req_id);
        if (it == by_req.end()) return;
        const auto request = pending.find(it->second);
        if (request == pending.end() || request->second.answer != type) return;
        complete_pending(it->second, Reply{payload, std::nullopt});
    }

    void on_error(const wire::Error& error) {
        if (error.req_id == 0) return;
        RbsbRequestError rejected{RbsbFailure::Rejected, error.code, error.message,
                                  std::chrono::milliseconds{error.retry_after_ms}, std::nullopt};
        if (const auto it = by_req.find(error.req_id); it != by_req.end()) {
            complete_pending(it->second, Reply{{}, std::move(rejected)});
            return;
        }
        const auto view = sub_by_req.find(error.req_id);
        if (view == sub_by_req.end()) return;
        const u64 id = view->second;
        sub_by_req.erase(view);
        Sub& sub = subs.at(id);
        sub_by_wire.erase(sub.sub_id);
        sub.sub_id = 0;
        sub.req_id = 0;
        sub.view_id.reset();
        sub.rejected = true;
        const std::shared_ptr<ViewStreamCallbacks> callbacks = sub.callbacks;
        if (callbacks->on_rejected) callbacks->on_rejected(rejected);
    }

    // ---- views -----------------------------------------------------------------------------

    [[nodiscard]] std::size_t active_views() const {
        return static_cast<std::size_t>(std::ranges::count_if(subs, [](const auto& entry) { return !entry.second.rejected; }));
    }

    Result<u64> subscribe(const wire::ViewSpec& view, u32 window, ViewStreamCallbacks callbacks) {
        if (edge && edge->limits.max_subscriptions != 0 && active_views() >= edge->limits.max_subscriptions)
            return make_diag(ErrorDomain::Browser, kTooManyViews).kind(ErrorKind::Conflict).fail();
        const u64 id = next_id++;
        Sub& sub = subs[id];
        sub.view = view;
        sub.window = window;
        sub.callbacks = std::make_shared<ViewStreamCallbacks>(std::move(callbacks));
        sub.lease = acquire_lease();
        if (serving()) send_subscribe(id);
        return id;
    }

    void send_subscribe(u64 id) {
        Sub& sub = subs.at(id);
        if (next_sub == 0) next_sub = 1;
        sub.sub_id = next_sub++;
        sub.req_id = next_request_id();
        sub_by_wire[sub.sub_id] = id;
        sub_by_req[sub.req_id] = id;
        // A failed send means the connection is closing; its close replays the view.
        (void)connection->send(*control, wire::frame_bytes(wire::Subscribe{sub.req_id, sub.sub_id, sub.view, sub.window}),
                               false);
    }

    void unsubscribe(u64 id) {
        const auto it = subs.find(id);
        if (it == subs.end()) return;
        Sub sub = std::move(it->second);
        subs.erase(it);
        if (sub.sub_id != 0) {
            sub_by_wire.erase(sub.sub_id);
            sub_by_req.erase(sub.req_id);
            if (serving()) (void)connection->send(*control, wire::frame_bytes(wire::Unsubscribe{sub.sub_id}), false);
        }
        release_lease(sub.lease);
    }

    void on_sub_open(const wire::SubOpen& open) {
        const auto it = sub_by_wire.find(open.sub_id);
        if (it == sub_by_wire.end()) return;
        const u64 id = it->second;
        Sub& sub = subs.at(id);
        sub_by_req.erase(sub.req_id);
        sub.view_id = open.view_id;
        const u64 gen = epoch;
        const std::shared_ptr<ViewStreamCallbacks> callbacks = sub.callbacks;
        if (callbacks->on_open) callbacks->on_open(open);
        if (gen != epoch) return;
        const auto orphan = orphans.find(open.view_id);
        if (orphan == orphans.end()) return;
        std::deque<OrphanFrame> frames = std::move(orphan->second);
        orphans.erase(orphan);
        std::erase(orphan_order, open.view_id);
        for (auto& frame : frames) {
            if (gen != epoch) return;
            const u32 view_id = open.view_id;
            on_view_frame(view_id, std::move(frame));
        }
    }

    void on_view_frame(u32 view_id, OrphanFrame frame) {
        std::vector<u64> targets;
        for (const auto& [id, sub] : subs)
            if (sub.view_id == view_id) targets.push_back(id);
        if (targets.empty()) {
            keep_orphan(view_id, std::move(frame));
            return;
        }
        const u64 gen = epoch;
        for (const u64 id : targets) {
            if (gen != epoch) return;
            const auto it = subs.find(id);
            if (it == subs.end() || it->second.view_id != view_id) continue;
            const std::shared_ptr<ViewStreamCallbacks> callbacks = it->second.callbacks;
            if (const auto* snapshot = std::get_if<wire::Snapshot>(&frame)) {
                if (callbacks->on_snapshot) callbacks->on_snapshot(*snapshot);
            } else if (callbacks->on_delta) {
                callbacks->on_delta(std::get<wire::Delta>(frame));
            }
        }
    }

    void keep_orphan(u32 view_id, OrphanFrame frame) {
        auto it = orphans.find(view_id);
        if (it == orphans.end()) {
            if (orphan_order.size() >= kOrphanViews) {
                orphans.erase(orphan_order.front());
                orphan_order.pop_front();
            }
            orphan_order.push_back(view_id);
            it = orphans.emplace(view_id, std::deque<OrphanFrame>{}).first;
        }
        // A snapshot replaces the view, so only frames from it on still matter.
        if (std::holds_alternative<wire::Snapshot>(frame)) it->second.clear();
        if (it->second.size() >= kOrphanFrames) it->second.pop_front();
        it->second.push_back(std::move(frame));
    }

    // ---- endpoint --------------------------------------------------------------------------

    void set_endpoint(RbsbEndpoint next) {
        if (next == endpoint) return;
        endpoint = std::move(next);
        if (phase == Phase::Idle) return;
        retry_timer.cancel();
        connect_timer.cancel();
        drain_at.reset();
        Fallout fallout = detach_connection();
        backoff.reset();
        start_attempt();
        notify(std::move(fallout));
    }

    BrowserSessionDeps deps;
    RbsbEndpoint endpoint;
    BrowserSessionOptions options;
    std::shared_ptr<Alive> alive;
    ConnectionStatus status;
    std::optional<EdgeSession> edge;
    FullJitterBackoff backoff;

    FlatSet<u64> leases;
    u64 next_id = 1;
    u32 next_req = 1;
    u32 next_sub = 1;

    Phase phase = Phase::Idle;
    // Bumped whenever the connection or attempt changes, so stale callbacks are dropped.
    u64 epoch = 0;
    std::unique_ptr<CancelSource> attempt;
    std::vector<Endpoint> candidates;
    std::size_t candidate = 0;
    ConnectFailureEvidence evidence;
    std::optional<Diagnostic> last_failure;
    TimerHandle connect_timer;
    TimerHandle retry_timer;
    std::optional<SteadyTime> drain_at;

    std::unique_ptr<ports::IQuicConnection> connection;
    std::optional<u64> control;
    std::optional<wire::StreamFramer> control_framer;
    // Server-opened streams; empty once a stream sent a malformed frame.
    std::map<u64, std::optional<wire::StreamFramer>> streams;

    std::map<u64, Pending> pending;
    std::map<u32, u64> by_req;
    std::map<u64, Sub> subs;
    std::map<u32, u64> sub_by_req;
    std::map<u32, u64> sub_by_wire;
    std::map<u32, std::deque<OrphanFrame>> orphans;
    std::deque<u32> orphan_order;
};

// ---- handles -------------------------------------------------------------------------------

BrowserLease::BrowserLease(BrowserLease&& other) noexcept
    : session_(std::exchange(other.session_, nullptr)), id_(std::exchange(other.id_, 0)) {}

BrowserLease& BrowserLease::operator=(BrowserLease&& other) noexcept {
    if (this != &other) {
        release();
        session_ = std::exchange(other.session_, nullptr);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

BrowserLease::~BrowserLease() { release(); }

void BrowserLease::release() {
    if (BrowserSession* session = std::exchange(session_, nullptr)) session->release_lease(std::exchange(id_, 0));
}

ViewSubscription::ViewSubscription(ViewSubscription&& other) noexcept
    : session_(std::exchange(other.session_, nullptr)), id_(std::exchange(other.id_, 0)) {}

ViewSubscription& ViewSubscription::operator=(ViewSubscription&& other) noexcept {
    if (this != &other) {
        reset();
        session_ = std::exchange(other.session_, nullptr);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

ViewSubscription::~ViewSubscription() { reset(); }

void ViewSubscription::reset() {
    if (BrowserSession* session = std::exchange(session_, nullptr)) session->unsubscribe(std::exchange(id_, 0));
}

// ---- session -------------------------------------------------------------------------------

BrowserSession::BrowserSession(BrowserSessionDeps deps, RbsbEndpoint endpoint, BrowserSessionOptions options)
    : impl_(std::make_unique<Impl>(deps, std::move(endpoint), std::move(options))) {}

BrowserSession::~BrowserSession() = default;

BrowserLease BrowserSession::acquire() { return BrowserLease(*this, impl_->acquire_lease()); }

const ConnectionStatus& BrowserSession::status() const noexcept { return impl_->status; }

const std::optional<EdgeSession>& BrowserSession::edge() const noexcept { return impl_->edge; }

void BrowserSession::set_endpoint(RbsbEndpoint endpoint) { impl_->set_endpoint(std::move(endpoint)); }

const RbsbEndpoint& BrowserSession::endpoint() const noexcept { return impl_->endpoint; }

Result<ViewSubscription> BrowserSession::subscribe(const sb::wire::ViewSpec& view, u32 window,
                                                   ViewStreamCallbacks callbacks) {
    Result<u64> id = impl_->subscribe(view, window, std::move(callbacks));
    if (!id) return std::unexpected(std::move(id.error()));
    return ViewSubscription(*this, *id);
}

void BrowserSession::query(sb::wire::Query query, CancelToken token,
                           UniqueFunction<void(RbsbResult<sb::wire::QueryResult>)> done) {
    impl_->issue(
        wire::FrameType::query_result,
        [query = std::move(query)](u32 req_id) mutable {
            query.req_id = req_id;
            return wire::frame_bytes(query);
        },
        completer<wire::QueryResult>(std::move(done)), token);
}

void BrowserSession::resolve(ServerId id, CancelToken token,
                             UniqueFunction<void(RbsbResult<sb::wire::ResolveResult>)> done) {
    impl_->issue(
        wire::FrameType::resolve_result,
        [uuid = id.value](u32 req_id) { return wire::frame_bytes(wire::Resolve{req_id, uuid}); },
        completer<wire::ResolveResult>(std::move(done)), token);
}

void BrowserSession::join(ServerId id, std::optional<SecretString> password, CancelToken token,
                          UniqueFunction<void(RbsbResult<sb::wire::JoinGrant>)> done) {
    impl_->issue(
        wire::FrameType::join_grant,
        [uuid = id.value, password = std::move(password)](u32 req_id) mutable {
            wire::Join join{req_id, uuid, std::nullopt};
            if (password) join.password = password->reveal();
            wire::Writer writer(kJoinFrameReserve + (join.password ? join.password->size() : 0));
            wire::encode_frame(writer, join);
            if (join.password) wipe(*join.password);
            password.reset();
            return writer.take();
        },
        completer<wire::JoinGrant>(std::move(done)), token);
}

void BrowserSession::release_lease(u64 id) { impl_->release_lease(id); }

void BrowserSession::unsubscribe(u64 id) { impl_->unsubscribe(id); }

}  // namespace reboot::browser
