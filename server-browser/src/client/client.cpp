#include "client/client.hpp"

#include <zstd.h>

#include <cstring>

namespace sb::client {

namespace {

struct SendBuf {
    QUIC_BUFFER qb{};
    std::vector<u8> bytes;
};

}  // namespace

// Per-stream context: the control stream, or a server-opened unidirectional stream carrying a
// snapshot (one frame, then FIN) or the delta fallback.
struct Client::InStream {
    Client* client;
    bool control;
    wire::StreamFramer framer{1 << 26};
};

ClientRuntime::ClientRuntime(Options opts) {
    const QUIC_REGISTRATION_CONFIG reg{"sb-client", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    quic::check(lib_->RegistrationOpen(&reg, &registration_), "RegistrationOpen");
    QUIC_SETTINGS s{};
    s.IsSet.IdleTimeoutMs = 1;
    s.IdleTimeoutMs = opts.idle_timeout_ms;
    s.IsSet.DatagramReceiveEnabled = 1;
    s.DatagramReceiveEnabled = 1;
    s.IsSet.PeerUnidiStreamCount = 1;
    s.PeerUnidiStreamCount = 32;  // snapshots and the delta fallback stream
    s.IsSet.PeerBidiStreamCount = 1;
    s.PeerBidiStreamCount = 0;
    s.IsSet.MaxAckDelayMs = 1;
    s.MaxAckDelayMs = opts.max_ack_delay_ms;
    if (opts.keepalive_ms) {
        s.IsSet.KeepAliveIntervalMs = 1;
        s.KeepAliveIntervalMs = opts.keepalive_ms;
    }
    const QUIC_BUFFER alpn{static_cast<u32>(wire::kAlpn.size()), reinterpret_cast<u8*>(const_cast<char*>(wire::kAlpn.data()))};
    quic::check(lib_->ConfigurationOpen(registration_, &alpn, 1, &s, sizeof(s), nullptr, &configuration_), "ConfigurationOpen");
    QUIC_CREDENTIAL_CONFIG cred{};
    cred.Type = QUIC_CREDENTIAL_TYPE_NONE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    if (opts.insecure) cred.Flags |= QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    if (!opts.ca_file.empty()) {
        cred.Flags |= QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE;
        cred.CaCertificateFile = opts.ca_file.c_str();
    }
    quic::check(lib_->ConfigurationLoadCredential(configuration_, &cred), "ConfigurationLoadCredential");
}

ClientRuntime::~ClientRuntime() {
    if (configuration_) lib_->ConfigurationClose(configuration_);
    if (registration_) lib_->RegistrationClose(registration_);
}

Client::Client(ClientRuntime& rt, Options opts) : rt_(rt), api_(rt.api()), opts_(std::move(opts)) {}

Client::~Client() {
    close();
    if (conn_) {
        // Closing the handle before the shutdown completes would drop the connection silently,
        // leaving the server to discover it only through the idle timeout.
        std::unique_lock lk(done_mu_);
        done_cv_.wait_for(lk, std::chrono::seconds(2), [&] { return shutdown_done_; });
        lk.unlock();
        api_->ConnectionClose(conn_);
    }
}

void Client::connect() {
    State expected = State::idle;
    if (!state_.compare_exchange_strong(expected, State::connecting)) return;
    quic::check(api_->ConnectionOpen(rt_.registration(), &Client::conn_cb, this, &conn_), "ConnectionOpen");
    if (opts_.local_address) {
        const QUIC_ADDR la = quic::quic_addr(*opts_.local_address, 0);
        (void)api_->SetParam(conn_, QUIC_PARAM_CONN_LOCAL_ADDRESS, sizeof(la), &la);
    }
    quic::check(api_->ConnectionStart(conn_, rt_.configuration(), QUIC_ADDRESS_FAMILY_UNSPEC, opts_.host.c_str(), opts_.port),
                "ConnectionStart");
}

void Client::close() {
    const State s = state_.exchange(State::closed);
    if (conn_ && s != State::closed && s != State::idle) api_->ConnectionShutdown(conn_, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
}

void Client::emit(Event e) {
    if (opts_.on_event) opts_.on_event(e);
}

void Client::send_bytes(std::vector<u8> bytes) {
    std::lock_guard lk(send_mu_);
    if (!control_) {
        queued_.push_back(std::move(bytes));
        return;
    }
    auto* sb = new SendBuf();
    sb->bytes = std::move(bytes);
    sb->qb.Buffer = sb->bytes.data();
    sb->qb.Length = static_cast<u32>(sb->bytes.size());
    if (QUIC_FAILED(api_->StreamSend(control_, &sb->qb, 1, QUIC_SEND_FLAG_NONE, sb))) delete sb;
}

u32 Client::subscribe(const wire::ViewSpec& view, u32 window) {
    const u32 sub = next_sub_.fetch_add(1);
    send(wire::Subscribe{.req_id = next_req(), .sub_id = sub, .view = view, .window = window});
    return sub;
}

void Client::unsubscribe(u32 sub_id) { send(wire::Unsubscribe{.sub_id = sub_id}); }

u32 Client::query(const wire::ViewSpec& view, std::string text, u32 limit, wire::Bytes cursor) {
    const u32 id = next_req();
    send(wire::Query{.req_id = id, .view = view, .text = std::move(text), .limit = limit, .cursor = std::move(cursor)});
    return id;
}

u32 Client::resolve(const Uuid& uuid) {
    const u32 id = next_req();
    send(wire::Resolve{.req_id = id, .id = uuid});
    return id;
}

u32 Client::join(const Uuid& uuid, std::optional<std::string> password) {
    const u32 id = next_req();
    send(wire::Join{.req_id = id, .id = uuid, .password = std::move(password)});
    return id;
}

u32 Client::host_register(wire::HostRegister msg) {
    msg.req_id = next_req();
    send(msg);
    return msg.req_id;
}

u32 Client::host_update(wire::HostUpdate msg, bool want_ack) {
    msg.req_id = want_ack ? next_req() : 0;
    send(msg);
    return msg.req_id;
}

u32 Client::host_unregister() {
    const u32 id = next_req();
    send(wire::HostUnregister{.req_id = id});
    return id;
}

void Client::heartbeat() {
    if (!conn_ || state_.load() != State::ready) return;
    auto* sb = new SendBuf();
    sb->bytes = wire::frame_bytes(wire::HostHeartbeat{.seq = heartbeat_seq_.fetch_add(1)}, wire::LenWidth::two);
    sb->qb.Buffer = sb->bytes.data();
    sb->qb.Length = static_cast<u32>(sb->bytes.size());
    if (QUIC_FAILED(api_->DatagramSend(conn_, &sb->qb, 1, QUIC_SEND_FLAG_NONE, sb))) delete sb;
}

void Client::on_frame(const wire::FrameView& f, bool datagram) {
    using wire::FrameType;
    auto deliver = [&]<class T>(T&& msg) {
        if (wire::decode_frame(f, msg)) emit(Event(std::forward<T>(msg)));
    };
    switch (f.type) {
        case FrameType::welcome:
            deliver(wire::Welcome{});
            break;
        case FrameType::sub_open: deliver(wire::SubOpen{}); break;
        case FrameType::query_result: deliver(wire::QueryResult{}); break;
        case FrameType::resolve_result: deliver(wire::ResolveResult{}); break;
        case FrameType::join_grant: deliver(wire::JoinGrant{}); break;
        case FrameType::host_registered: deliver(wire::HostRegistered{}); break;
        case FrameType::ack: deliver(wire::Ack{}); break;
        case FrameType::error: deliver(wire::Error{}); break;
        case FrameType::go_away: deliver(wire::GoAway{}); break;
        case FrameType::host_status: deliver(wire::HostStatus{}); break;
        case FrameType::snapshot: {
            SnapshotEvent e;
            if (wire::decode_frame(f, e.snapshot)) emit(Event(std::move(e)));
            break;
        }
        case FrameType::snapshot_zstd: {
            wire::Reader r(f.payload);
            const u64 raw_len = r.varint();
            if (!r.ok() || raw_len > (64u << 20)) break;
            std::vector<u8> raw(raw_len);
            const std::size_t n = ZSTD_decompress(raw.data(), raw.size(), r.pos(), r.remaining());
            SnapshotEvent e;
            if (!ZSTD_isError(n) && n == raw_len && wire::decode(raw, e.snapshot)) emit(Event(std::move(e)));
            break;
        }
        case FrameType::delta: {
            DeltaEvent e{.via_datagram = datagram};
            if (opts_.decode_deltas) {
                if (!wire::decode_frame(f, e.delta)) break;
                e.view_id = e.delta.view_id;
                e.patch_count = static_cast<u32>(e.delta.patches.size());
            } else {
                // Walk the top-level fields only: view_id (1) and one length-delimited patch each (2).
                wire::Reader r(f.payload);
                while (r.ok() && !r.empty()) {
                    const u64 key = r.varint();
                    if ((key & 7) == 0) {
                        const u64 v = r.varint();
                        if ((key >> 3) == 1) e.view_id = static_cast<u32>(v);
                    } else if ((key & 7) == 2) {
                        (void)r.bytes(r.varint());
                        if ((key >> 3) == 2) ++e.patch_count;
                    } else {
                        r.fail();
                    }
                }
                if (!r.ok()) break;
            }
            emit(Event(std::move(e)));
            break;
        }
        default: break;
    }
}

QUIC_STATUS QUIC_API Client::conn_cb(HQUIC, void* ctx, QUIC_CONNECTION_EVENT* ev) {
    auto* self = static_cast<Client*>(ctx);
    const QUIC_API_TABLE* api = self->api_;
    switch (ev->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            HQUIC s = nullptr;
            auto* sc = new InStream{self, true};
            if (QUIC_FAILED(api->StreamOpen(self->conn_, QUIC_STREAM_OPEN_FLAG_NONE, &Client::stream_cb, sc, &s))) {
                delete sc;
                api->ConnectionShutdown(self->conn_, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 1);
                break;
            }
            if (QUIC_FAILED(api->StreamStart(s, QUIC_STREAM_START_FLAG_IMMEDIATE))) {
                api->StreamClose(s);
                delete sc;
                api->ConnectionShutdown(self->conn_, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 1);
                break;
            }
            std::vector<std::vector<u8>> queued;
            {
                std::lock_guard lk(self->send_mu_);
                self->control_ = s;
                queued.swap(self->queued_);
            }
            self->send(wire::Hello{.role = self->opts_.role,
                                   .proto_minor = wire::kProtoMinor,
                                   .client_version = self->opts_.client_version,
                                   .features = self->opts_.features});
            for (auto& q : queued) self->send_bytes(std::move(q));
            State exp = State::connecting;
            self->state_.compare_exchange_strong(exp, State::ready);
            self->emit(Connected{});
            break;
        }
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            auto* in = new InStream{self, false};
            api->SetCallbackHandler(ev->PEER_STREAM_STARTED.Stream, reinterpret_cast<void*>(&Client::stream_cb), in);
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
            const QUIC_BUFFER* b = ev->DATAGRAM_RECEIVED.Buffer;
            (void)wire::for_each_frame(std::span<const u8>(b->Buffer, b->Length), 1500, [&](const wire::FrameView& f) {
                self->on_frame(f, true);
                return true;
            });
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
            if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(ev->DATAGRAM_SEND_STATE_CHANGED.State))
                delete static_cast<SendBuf*>(ev->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            self->state_.store(State::closed);
            {
                std::lock_guard lk(self->done_mu_);
                self->shutdown_done_ = true;
            }
            self->done_cv_.notify_all();
            {
                std::lock_guard lk(self->send_mu_);
                self->control_ = nullptr;
            }
            self->emit(Closed{.reason = "closed"});
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            self->emit(Closed{.reason = "closed by server", .app_error = ev->SHUTDOWN_INITIATED_BY_PEER.ErrorCode});
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            self->emit(Closed{.reason = "transport shutdown"});
            break;
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API Client::stream_cb(HQUIC stream, void* ctx, QUIC_STREAM_EVENT* ev) {
    auto* sc = static_cast<InStream*>(ctx);
    Client* self = sc->client;
    const QUIC_API_TABLE* api = self->api_;
    switch (ev->Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            for (u32 i = 0; i < ev->RECEIVE.BufferCount; ++i) {
                std::span<const u8> data(ev->RECEIVE.Buffers[i].Buffer, ev->RECEIVE.Buffers[i].Length);
                (void)sc->framer.feed(data, [&](const wire::FrameView& f) {
                    self->on_frame(f, false);
                    return true;
                });
            }
            break;
        case QUIC_STREAM_EVENT_SEND_COMPLETE: delete static_cast<SendBuf*>(ev->SEND_COMPLETE.ClientContext); break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (sc->control) {
                std::lock_guard lk(self->send_mu_);
                self->control_ = nullptr;
            }
            delete sc;
            api->StreamClose(stream);
            break;
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

}  // namespace sb::client
