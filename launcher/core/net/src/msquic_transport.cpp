#include "reboot/net/msquic_transport.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <msquic.h>

#include "messages.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::net {

namespace {

// How long closing waits for the peer to acknowledge before the handle is dropped regardless.
constexpr std::chrono::seconds kShutdownWait{2};
// rbsb edges open unidirectional streams for snapshots and the delta fallback.
constexpr u16 kPeerUnidiStreams = 32;

[[nodiscard]] std::string status_text(QUIC_STATUS status) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out = "status 0x";
    for (int shift = 28; shift >= 0; shift -= 4) out.push_back(kDigits[(static_cast<u32>(status) >> shift) & 0xF]);
    return out;
}

[[nodiscard]] Diagnostic quic_unavailable(QUIC_STATUS status) {
    return make_diag(ErrorDomain::Net, kQuicUnavailable).detail(status_text(status));
}

// Frames can carry a join password, so every buffer is wiped once MsQuic is done with it.
struct SendBuffer {
    SendBuffer() = default;
    SendBuffer(const SendBuffer&) = delete;
    SendBuffer& operator=(const SendBuffer&) = delete;
    ~SendBuffer() { secure_wipe(bytes.data(), bytes.size()); }

    QUIC_BUFFER buffer{};
    std::vector<u8> bytes;
};

[[nodiscard]] SendBuffer* make_send_buffer(std::vector<u8> bytes) {
    auto* send = new SendBuffer();
    send->bytes = std::move(bytes);
    send->buffer.Buffer = send->bytes.data();
    send->buffer.Length = static_cast<u32>(send->bytes.size());
    return send;
}

struct Connection;

struct StreamContext {
    Connection* connection = nullptr;
    u64 id = 0;
};

// Callbacks take only `mutex`; our calls into MsQuic hold `api_mutex`, since some of them wait
// for MsQuic threads that may be in a callback. StreamSend never waits, so it runs under `mutex`,
// which keeps the stream's handle from being closed under it.
struct Connection {
    Connection(const QUIC_API_TABLE* api_in, std::string host_in, bool ipv4_only_in)
        : api(api_in), host(std::move(host_in)), ipv4_only(ipv4_only_in) {}

    const QUIC_API_TABLE* api;
    const std::string host;
    const bool ipv4_only;
    HQUIC handle = nullptr;
    HQUIC configuration = nullptr;

    std::mutex api_mutex;
    // Set under api_mutex once the handle is being closed; no call into MsQuic follows.
    bool closing = false;
    u64 next_local_stream = 0;

    std::mutex mutex;
    std::condition_variable shutdown_done;
    bool connected = false;
    bool complete = false;
    // Set once the owner went away; callbacks are dropped from then on.
    bool detached = false;
    ports::QuicCallbacks callbacks;
    std::optional<Diagnostic> failure;
    // Open streams; a stream leaves when its shutdown completes, and its callback closes it.
    std::map<u64, HQUIC> streams;

    template <class F>
    void deliver(F&& invoke) {
        const std::scoped_lock lock(mutex);
        if (!detached) invoke(callbacks);
    }

    [[nodiscard]] Diagnostic failure_for(QUIC_STATUS status) const {
        DiagBuilder builder = [&] {
            if (!connected && (status == QUIC_STATUS_CONNECTION_IDLE || status == QUIC_STATUS_CONNECTION_TIMEOUT))
                return make_diag(ErrorDomain::Net, kQuicUdpBlocked).arg("host", host).retryable();
            // An IPv4-only connection with no route to the host means this machine has no IPv4 path out.
            if (!connected && ipv4_only && status == QUIC_STATUS_UNREACHABLE)
                return make_diag(ErrorDomain::Net, kQuicNoIpv4).retryable();
            if (!connected) return make_diag(ErrorDomain::Net, kQuicConnectFailed).arg("host", host).retryable();
            return make_diag(ErrorDomain::Net, kQuicConnectionLost).arg("host", host).retryable();
        }();
        std::move(builder).detail(status_text(status));
        return std::move(builder).build();
    }

    [[nodiscard]] Diagnostic lost(std::optional<QUIC_STATUS> status = std::nullopt) const {
        DiagBuilder builder = make_diag(ErrorDomain::Net, kQuicConnectionLost).arg("host", host);
        if (status) std::move(builder).detail(status_text(*status));
        return std::move(builder).build();
    }
};

QUIC_STATUS QUIC_API on_stream_event(HQUIC stream, void* context, QUIC_STREAM_EVENT* event) {
    auto* stream_context = static_cast<StreamContext*>(context);
    Connection& connection = *stream_context->connection;
    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            const bool fin = (event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) != 0;
            const u32 count = event->RECEIVE.BufferCount;
            connection.deliver([&](ports::QuicCallbacks& callbacks) {
                if (!callbacks.on_stream_data) return;
                if (count == 0) {
                    callbacks.on_stream_data(stream_context->id, {}, fin);
                    return;
                }
                for (u32 i = 0; i < count; ++i) {
                    const QUIC_BUFFER& buffer = event->RECEIVE.Buffers[i];
                    callbacks.on_stream_data(stream_context->id, std::span<const u8>(buffer.Buffer, buffer.Length),
                                             fin && i + 1 == count);
                }
            });
            break;
        }
        case QUIC_STREAM_EVENT_SEND_COMPLETE: delete static_cast<SendBuffer*>(event->SEND_COMPLETE.ClientContext); break;
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED: connection.api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0); break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
            {
                const std::scoped_lock lock(connection.mutex);
                connection.streams.erase(stream_context->id);
            }
            // MsQuic allows StreamClose here; during ConnectionClose it closes the handle itself.
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) connection.api->StreamClose(stream);
            delete stream_context;
            break;
        }
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API on_connection_event(HQUIC, void* context, QUIC_CONNECTION_EVENT* event) {
    auto& connection = *static_cast<Connection*>(context);
    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            {
                const std::scoped_lock lock(connection.mutex);
                connection.connected = true;
            }
            connection.deliver([](ports::QuicCallbacks& callbacks) {
                if (callbacks.on_connected) callbacks.on_connected();
            });
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT: {
            const QUIC_STATUS status = event->SHUTDOWN_INITIATED_BY_TRANSPORT.Status;
            const std::scoped_lock lock(connection.mutex);
            if (!connection.failure) connection.failure = connection.failure_for(status);
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER: {
            const QUIC_UINT62 code = event->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
            const std::scoped_lock lock(connection.mutex);
            if (!connection.failure && code != 0)
                connection.failure = make_diag(ErrorDomain::Net, kQuicConnectionLost)
                                         .arg("host", connection.host)
                                         .arg("app_error", static_cast<u64>(code))
                                         .build();
            break;
        }
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            HQUIC stream = event->PEER_STREAM_STARTED.Stream;
            u64 id = 0;
            u32 size = sizeof(id);
            if (QUIC_FAILED(connection.api->GetParam(stream, QUIC_PARAM_STREAM_ID, &size, &id))) {
                connection.api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
                break;
            }
            {
                const std::scoped_lock lock(connection.mutex);
                connection.streams[id] = stream;
            }
            connection.api->SetCallbackHandler(stream, reinterpret_cast<void*>(&on_stream_event), new StreamContext{&connection, id});
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED: {
            const QUIC_BUFFER* buffer = event->DATAGRAM_RECEIVED.Buffer;
            connection.deliver([&](ports::QuicCallbacks& callbacks) {
                if (callbacks.on_datagram) callbacks.on_datagram(std::span<const u8>(buffer->Buffer, buffer->Length));
            });
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
            if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(event->DATAGRAM_SEND_STATE_CHANGED.State))
                delete static_cast<SendBuffer*>(event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
            std::optional<Diagnostic> failure;
            UniqueFunction<void(std::optional<Diagnostic>)> on_closed;
            {
                const std::scoped_lock lock(connection.mutex);
                connection.complete = true;
                failure = std::move(connection.failure);
                if (!connection.detached) on_closed = std::move(connection.callbacks.on_closed);
            }
            connection.shutdown_done.notify_all();
            if (on_closed) on_closed(std::move(failure));
            break;
        }
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

// Closes the connection's handle; ConnectionClose waits for callbacks that are running.
void close_handle(Connection& connection) {
    {
        const std::scoped_lock lock(connection.api_mutex);
        if (connection.closing) return;
        connection.closing = true;
    }
    {
        std::unique_lock lock(connection.mutex);
        connection.detached = true;
        connection.callbacks = {};
        if (!connection.complete) {
            lock.unlock();
            connection.api->ConnectionShutdown(connection.handle, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            lock.lock();
            connection.shutdown_done.wait_for(lock, kShutdownWait, [&] { return connection.complete; });
        }
    }
    connection.api->ConnectionClose(connection.handle);
    if (connection.configuration != nullptr) connection.api->ConfigurationClose(connection.configuration);
}

// Shared by the transport and its connections, so whichever goes first closes the handles.
struct Registry {
    std::mutex mutex;
    bool open = true;
    std::set<Connection*> live;
};

class MsQuicConnection final : public ports::IQuicConnection {
public:
    MsQuicConnection(std::shared_ptr<Registry> registry, std::unique_ptr<Connection> connection)
        : registry_(std::move(registry)), connection_(std::move(connection)) {}
    MsQuicConnection(const MsQuicConnection&) = delete;
    MsQuicConnection& operator=(const MsQuicConnection&) = delete;

    ~MsQuicConnection() override {
        const std::scoped_lock lock(registry_->mutex);
        registry_->live.erase(connection_.get());
        if (registry_->open) close_handle(*connection_);
    }

    Result<u64> open_stream() override {
        Connection& connection = *connection_;
        const std::scoped_lock api_lock(connection.api_mutex);
        if (connection.closing) return std::unexpected(connection.lost());
        {
            const std::scoped_lock lock(connection.mutex);
            if (connection.complete) return std::unexpected(connection.lost());
        }
        // Client-initiated bidirectional ids, in the order the streams start.
        const u64 id = connection.next_local_stream;
        auto* context = new StreamContext{&connection, id};
        HQUIC stream = nullptr;
        QUIC_STATUS status = connection.api->StreamOpen(connection.handle, QUIC_STREAM_OPEN_FLAG_NONE, &on_stream_event, context, &stream);
        if (QUIC_FAILED(status)) {
            delete context;
            return std::unexpected(connection.lost(status));
        }
        {
            const std::scoped_lock lock(connection.mutex);
            connection.streams[id] = stream;
        }
        status = connection.api->StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
        if (QUIC_FAILED(status)) {
            {
                const std::scoped_lock lock(connection.mutex);
                connection.streams.erase(id);
            }
            // A stream that never started gets no events, so its context is ours to free.
            connection.api->StreamClose(stream);
            delete context;
            return std::unexpected(connection.lost(status));
        }
        connection.next_local_stream += 4;
        return id;
    }

    Result<void> send(u64 stream_id, std::vector<u8> bytes, bool fin) override {
        Connection& connection = *connection_;
        // Owned from the start, so the bytes are wiped on every failure path too.
        std::unique_ptr<SendBuffer> buffer(make_send_buffer(std::move(bytes)));
        const std::scoped_lock api_lock(connection.api_mutex);
        if (connection.closing) return std::unexpected(connection.lost());
        QUIC_STATUS status = QUIC_STATUS_SUCCESS;
        {
            const std::scoped_lock lock(connection.mutex);
            const auto it = connection.streams.find(stream_id);
            if (it == connection.streams.end())
                return make_diag(ErrorDomain::Net, kQuicStreamUnknown)
                    .arg("stream", stream_id)
                    .arg("host", connection.host)
                    .kind(ErrorKind::InvalidInput)
                    .fail();
            status = connection.api->StreamSend(it->second, &buffer->buffer, 1, fin ? QUIC_SEND_FLAG_FIN : QUIC_SEND_FLAG_NONE,
                                                buffer.get());
            // SEND_COMPLETE now owns it and may already have freed it.
            if (QUIC_SUCCEEDED(status)) (void)buffer.release();
        }
        if (QUIC_FAILED(status)) return std::unexpected(connection.lost(status));
        return {};
    }

    Result<void> send_datagram(std::vector<u8> bytes) override {
        Connection& connection = *connection_;
        const std::scoped_lock api_lock(connection.api_mutex);
        if (connection.closing) return std::unexpected(connection.lost());
        SendBuffer* buffer = make_send_buffer(std::move(bytes));
        const QUIC_STATUS status = connection.api->DatagramSend(connection.handle, &buffer->buffer, 1, QUIC_SEND_FLAG_NONE, buffer);
        if (QUIC_FAILED(status)) {
            delete buffer;
            return std::unexpected(connection.lost(status));
        }
        return {};
    }

    void close(u64 app_error) override {
        Connection& connection = *connection_;
        const std::scoped_lock api_lock(connection.api_mutex);
        if (connection.closing) return;
        connection.api->ConnectionShutdown(connection.handle, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, app_error);
    }

private:
    std::shared_ptr<Registry> registry_;
    std::unique_ptr<Connection> connection_;
};

}  // namespace

struct MsQuicTransport::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        {
            const std::scoped_lock lock(registry->mutex);
            for (Connection* connection : registry->live) close_handle(*connection);
            registry->live.clear();
            registry->open = false;
        }
        if (registration != nullptr) api->RegistrationClose(registration);
        if (api != nullptr) MsQuicClose(api);
    }

    const QUIC_API_TABLE* api = nullptr;
    HQUIC registration = nullptr;
    std::optional<NativePath> system_ca;
    std::shared_ptr<Registry> registry = std::make_shared<Registry>();
};

Result<std::unique_ptr<MsQuicTransport>> MsQuicTransport::create(const ports::ISystemInfo& system) {
    auto impl = std::make_unique<Impl>();
    if (const QUIC_STATUS status = MsQuicOpen2(&impl->api); QUIC_FAILED(status)) {
        impl->api = nullptr;
        return std::unexpected(quic_unavailable(status));
    }
    const QUIC_REGISTRATION_CONFIG config{"reboot-engine", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    if (const QUIC_STATUS status = impl->api->RegistrationOpen(&config, &impl->registration); QUIC_FAILED(status)) {
        impl->registration = nullptr;
        return std::unexpected(quic_unavailable(status));
    }
    impl->system_ca = system.ca_bundle();
    return std::unique_ptr<MsQuicTransport>(new MsQuicTransport(std::move(impl)));
}

MsQuicTransport::MsQuicTransport(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

MsQuicTransport::~MsQuicTransport() = default;

Result<std::unique_ptr<ports::IQuicConnection>> MsQuicTransport::open_connection(const ports::QuicConnectOptions& options,
                                                                                 ports::QuicCallbacks callbacks) {
    const QUIC_API_TABLE* api = impl_->api;
    const auto connect_failed = [&](QUIC_STATUS status) {
        return make_diag(ErrorDomain::Net, kQuicConnectFailed)
            .arg("host", options.host)
            .detail(status_text(status))
            .retryable()
            .fail();
    };

    QUIC_SETTINGS settings{};
    settings.IsSet.HandshakeIdleTimeoutMs = 1;
    settings.HandshakeIdleTimeoutMs = static_cast<u64>(default_deadline(OpKind::QuicConnect).count());
    settings.IsSet.DatagramReceiveEnabled = 1;
    settings.DatagramReceiveEnabled = 1;
    settings.IsSet.PeerUnidiStreamCount = 1;
    settings.PeerUnidiStreamCount = kPeerUnidiStreams;
    settings.IsSet.PeerBidiStreamCount = 1;
    settings.PeerBidiStreamCount = 0;
    if (options.keepalive.count() > 0) {
        settings.IsSet.KeepAliveIntervalMs = 1;
        settings.KeepAliveIntervalMs = static_cast<u32>(options.keepalive.count());
    }
    const QUIC_BUFFER alpn{static_cast<u32>(options.alpn.size()), reinterpret_cast<u8*>(const_cast<char*>(options.alpn.data()))};

    auto connection = std::make_unique<Connection>(api, options.host, options.ipv4_only);
    if (const QUIC_STATUS status = api->ConfigurationOpen(impl_->registration, &alpn, 1, &settings, sizeof(settings), nullptr,
                                                          &connection->configuration);
        QUIC_FAILED(status)) {
        connection->configuration = nullptr;
        return connect_failed(status);
    }
    QUIC_CREDENTIAL_CONFIG credentials{};
    credentials.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credentials.Flags = QUIC_CREDENTIAL_FLAG_CLIENT;
    const std::optional<NativePath>& ca = options.ca_bundle ? options.ca_bundle : impl_->system_ca;
    const std::string ca_text = ca ? display_utf8(*ca) : std::string();
    if (ca) {
        credentials.Flags |= QUIC_CREDENTIAL_FLAG_SET_CA_CERTIFICATE_FILE;
        credentials.CaCertificateFile = ca_text.c_str();
    }
    if (const QUIC_STATUS status = api->ConfigurationLoadCredential(connection->configuration, &credentials); QUIC_FAILED(status)) {
        api->ConfigurationClose(connection->configuration);
        return connect_failed(status);
    }

    connection->callbacks = std::move(callbacks);
    if (const QUIC_STATUS status = api->ConnectionOpen(impl_->registration, &on_connection_event, connection.get(), &connection->handle);
        QUIC_FAILED(status)) {
        api->ConfigurationClose(connection->configuration);
        return connect_failed(status);
    }
    QUIC_ADDRESS_FAMILY family = options.ipv4_only ? QUIC_ADDRESS_FAMILY_INET : QUIC_ADDRESS_FAMILY_UNSPEC;
    if (options.remote) {
        family = options.remote->is_v4() ? QUIC_ADDRESS_FAMILY_INET : QUIC_ADDRESS_FAMILY_INET6;
        QUIC_ADDR remote{};
        QuicAddrSetFamily(&remote, family);
        if (options.remote->is_v4()) std::memcpy(&remote.Ipv4.sin_addr, options.remote->bytes.data() + 12, 4);
        else std::memcpy(&remote.Ipv6.sin6_addr, options.remote->bytes.data(), 16);
        QuicAddrSetPort(&remote, options.port.value);
        if (const QUIC_STATUS status =
                api->SetParam(connection->handle, QUIC_PARAM_CONN_REMOTE_ADDRESS, sizeof(remote), &remote);
            QUIC_FAILED(status)) {
            api->ConnectionClose(connection->handle);
            api->ConfigurationClose(connection->configuration);
            return connect_failed(status);
        }
    }
    const QUIC_STATUS started =
        api->ConnectionStart(connection->handle, connection->configuration, family, options.host.c_str(), options.port.value);
    if (QUIC_FAILED(started)) {
        api->ConnectionClose(connection->handle);
        api->ConfigurationClose(connection->configuration);
        return connect_failed(started);
    }
    {
        const std::scoped_lock lock(impl_->registry->mutex);
        impl_->registry->live.insert(connection.get());
    }
    return std::make_unique<MsQuicConnection>(impl_->registry, std::move(connection));
}

}  // namespace reboot::net
