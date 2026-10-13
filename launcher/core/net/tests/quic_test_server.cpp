#include "quic_test_server.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <msquic.h>

#ifdef _WIN32
#include <wincrypt.h>
#include <ncrypt.h>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")
#endif

namespace rb::net::test {

#ifdef _WIN32
// Schannel takes no PKCS#12, only a certificate whose key it can reach, so the key is imported
// into the user's key storage for the server's lifetime and deleted afterwards.
class ServerCertificate {
public:
    explicit ServerCertificate(const TestPki& pki) {
        CRYPT_DATA_BLOB blob{static_cast<DWORD>(pki.server_pkcs12.size()), const_cast<BYTE*>(pki.server_pkcs12.data())};
        const std::wstring password(kPkcs12Password, kPkcs12Password + std::strlen(kPkcs12Password));
        store_ = PFXImportCertStore(&blob, password.c_str(), CRYPT_USER_KEYSET | PKCS12_ALWAYS_CNG_KSP);
        REQUIRE(store_ != nullptr);
        certificate_ = CertFindCertificateInStore(store_, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                                  CERT_FIND_HAS_PRIVATE_KEY, nullptr, nullptr);
        REQUIRE(certificate_ != nullptr);
    }
    ~ServerCertificate() {
        if (certificate_ != nullptr) {
            NCRYPT_KEY_HANDLE key = 0;
            DWORD spec = 0;
            BOOL owned = FALSE;
            if (CryptAcquireCertificatePrivateKey(certificate_, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG, nullptr, &key, &spec,
                                                  &owned))
                NCryptDeleteKey(key, 0);
            CertFreeCertificateContext(certificate_);
        }
        if (store_ != nullptr) CertCloseStore(store_, 0);
    }
    ServerCertificate(const ServerCertificate&) = delete;
    ServerCertificate& operator=(const ServerCertificate&) = delete;

    void apply(QUIC_CREDENTIAL_CONFIG& credentials) const {
        credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_CONTEXT;
        credentials.CertificateContext = reinterpret_cast<QUIC_CERTIFICATE*>(const_cast<CERT_CONTEXT*>(certificate_));
    }

private:
    HCERTSTORE store_ = nullptr;
    PCCERT_CONTEXT certificate_ = nullptr;
};
#else
class ServerCertificate {
public:
    explicit ServerCertificate(const TestPki& pki)
        : pkcs12_{pki.server_pkcs12.data(), static_cast<u32>(pki.server_pkcs12.size()), kPkcs12Password} {}

    void apply(QUIC_CREDENTIAL_CONFIG& credentials) const {
        credentials.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;
        credentials.CertificatePkcs12 = const_cast<QUIC_CERTIFICATE_PKCS12*>(&pkcs12_);
    }

private:
    QUIC_CERTIFICATE_PKCS12 pkcs12_;
};
#endif

// Cleans up whatever the constructor got to, so a failed REQUIRE leaks nothing.
struct QuicTestServer::Impl {
    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        if (api == nullptr) return;
        if (listener != nullptr) api->ListenerClose(listener);
        if (registration != nullptr) api->RegistrationShutdown(registration, QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, 0);
        if (configuration != nullptr) api->ConfigurationClose(configuration);
        // Waits until every connection's SHUTDOWN_COMPLETE closed its handle.
        if (registration != nullptr) api->RegistrationClose(registration);
        MsQuicClose(api);
    }

    const QUIC_API_TABLE* api = nullptr;
    HQUIC registration = nullptr;
    HQUIC configuration = nullptr;
    HQUIC listener = nullptr;
    std::unique_ptr<ServerCertificate> certificate;
    std::string push;
    u16 port = 0;
};

namespace {

constexpr char kAlpn[] = "rbsb/1";

// MsQuic may complete a connection's shutdown before its streams', so the connection closes the
// streams still open before its own handle, or they would keep the registration alive.
struct ServerConnection {
    QuicTestServer::Impl* server = nullptr;
    std::mutex mutex;
    std::set<HQUIC> streams;
};

struct ServerStream {
    ServerConnection* connection = nullptr;
    std::vector<u8> received;
};

ServerStream* track(ServerConnection& connection, HQUIC stream) {
    const std::scoped_lock lock(connection.mutex);
    connection.streams.insert(stream);
    return new ServerStream{&connection, {}};
}

struct Send {
    QUIC_BUFFER buffer{};
    std::vector<u8> bytes;
};

void send_and_end(const QUIC_API_TABLE* api, HQUIC stream, std::vector<u8> bytes) {
    auto* send = new Send{};
    send->bytes = std::move(bytes);
    send->buffer.Buffer = send->bytes.data();
    send->buffer.Length = static_cast<u32>(send->bytes.size());
    if (QUIC_FAILED(api->StreamSend(stream, &send->buffer, 1, QUIC_SEND_FLAG_FIN, send))) delete send;
}

QUIC_STATUS QUIC_API on_server_stream(HQUIC stream, void* context, QUIC_STREAM_EVENT* event) {
    auto* state = static_cast<ServerStream*>(context);
    const QUIC_API_TABLE* api = state->connection->server->api;
    switch (event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            for (u32 i = 0; i < event->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER& buffer = event->RECEIVE.Buffers[i];
                state->received.insert(state->received.end(), buffer.Buffer, buffer.Buffer + buffer.Length);
            }
            if ((event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) != 0) send_and_end(api, stream, state->received);
            break;
        case QUIC_STREAM_EVENT_SEND_COMPLETE: delete static_cast<Send*>(event->SEND_COMPLETE.ClientContext); break;
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED: api->StreamShutdown(stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0); break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
            bool owned = false;
            {
                const std::scoped_lock lock(state->connection->mutex);
                owned = state->connection->streams.erase(stream) != 0;
            }
            if (!owned) break;
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) api->StreamClose(stream);
            delete state;
            break;
        }
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API on_server_connection(HQUIC connection, void* context, QUIC_CONNECTION_EVENT* event) {
    auto* state = static_cast<ServerConnection*>(context);
    QuicTestServer::Impl* server = state->server;
    const QUIC_API_TABLE* api = server->api;
    switch (event->Type) {
        case QUIC_CONNECTION_EVENT_CONNECTED: {
            if (server->push.empty()) break;
            HQUIC stream = nullptr;
            if (QUIC_FAILED(api->StreamOpen(connection, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, &on_server_stream, nullptr, &stream)))
                break;
            ServerStream* pushed = track(*state, stream);
            api->SetContext(stream, pushed);
            if (QUIC_FAILED(api->StreamStart(stream, QUIC_STREAM_START_FLAG_NONE))) {
                {
                    const std::scoped_lock lock(state->mutex);
                    state->streams.erase(stream);
                }
                api->StreamClose(stream);
                delete pushed;
                break;
            }
            send_and_end(api, stream, std::vector<u8>(server->push.begin(), server->push.end()));
            break;
        }
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            HQUIC stream = event->PEER_STREAM_STARTED.Stream;
            api->SetCallbackHandler(stream, reinterpret_cast<void*>(&on_server_stream), track(*state, stream));
            break;
        }
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: {
            std::set<HQUIC> open;
            {
                const std::scoped_lock lock(state->mutex);
                open.swap(state->streams);
            }
            for (HQUIC stream : open) {
                auto* stream_state = static_cast<ServerStream*>(api->GetContext(stream));
                api->StreamClose(stream);
                delete stream_state;
            }
            if (!event->SHUTDOWN_COMPLETE.AppCloseInProgress) api->ConnectionClose(connection);
            delete state;
            break;
        }
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API on_listener(HQUIC, void* context, QUIC_LISTENER_EVENT* event) {
    if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION) return QUIC_STATUS_SUCCESS;
    auto* server = static_cast<QuicTestServer::Impl*>(context);
    HQUIC connection = event->NEW_CONNECTION.Connection;
    server->api->SetCallbackHandler(connection, reinterpret_cast<void*>(&on_server_connection),
                                    new ServerConnection{server, {}, {}});
    return server->api->ConnectionSetConfiguration(connection, server->configuration);
}

}  // namespace

QuicTestServer::QuicTestServer(const TestPki& pki, std::string push) : impl_(std::make_unique<Impl>()) {
    impl_->push = std::move(push);
    REQUIRE(QUIC_SUCCEEDED(MsQuicOpen2(&impl_->api)));
    const QUIC_API_TABLE* api = impl_->api;
    const QUIC_REGISTRATION_CONFIG registration{"reboot-net-test-server", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    REQUIRE(QUIC_SUCCEEDED(api->RegistrationOpen(&registration, &impl_->registration)));

    QUIC_SETTINGS settings{};
    settings.IsSet.IdleTimeoutMs = 1;
    settings.IdleTimeoutMs = 10'000;
    settings.IsSet.PeerBidiStreamCount = 1;
    settings.PeerBidiStreamCount = 16;
    const QUIC_BUFFER alpn{static_cast<u32>(sizeof(kAlpn) - 1), reinterpret_cast<u8*>(const_cast<char*>(kAlpn))};
    REQUIRE(QUIC_SUCCEEDED(api->ConfigurationOpen(impl_->registration, &alpn, 1, &settings, sizeof(settings), nullptr,
                                                  &impl_->configuration)));
    impl_->certificate = std::make_unique<ServerCertificate>(pki);
    QUIC_CREDENTIAL_CONFIG credentials{};
    credentials.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    impl_->certificate->apply(credentials);
    const QUIC_STATUS loaded = api->ConfigurationLoadCredential(impl_->configuration, &credentials);
    INFO("ConfigurationLoadCredential: " << static_cast<long long>(loaded));
    REQUIRE(QUIC_SUCCEEDED(loaded));

    REQUIRE(QUIC_SUCCEEDED(api->ListenerOpen(impl_->registration, &on_listener, impl_.get(), &impl_->listener)));
    QUIC_ADDR address{};
    QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_INET);
    const u8 loopback[4] = {127, 0, 0, 1};
    std::memcpy(&address.Ipv4.sin_addr, loopback, sizeof(loopback));
    QuicAddrSetPort(&address, 0);
    REQUIRE(QUIC_SUCCEEDED(api->ListenerStart(impl_->listener, &alpn, 1, &address)));
    u32 size = sizeof(address);
    REQUIRE(QUIC_SUCCEEDED(api->GetParam(impl_->listener, QUIC_PARAM_LISTENER_LOCAL_ADDRESS, &size, &address)));
    impl_->port = QuicAddrGetPort(&address);
}

QuicTestServer::~QuicTestServer() = default;

u16 QuicTestServer::port() const { return impl_->port; }

}  // namespace rb::net::test
