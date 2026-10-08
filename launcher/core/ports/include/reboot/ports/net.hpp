#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ports {

// `wine_server` marks a port held by wineserver on behalf of a Windows process.
struct PortOwner {
    u32 pid = 0;
    std::optional<NativePath> exe;
    bool wine_server = false;
};

class IPortInspector {
public:
    virtual ~IPortInspector() = default;
    virtual Result<std::optional<PortOwner>> tcp_owner(Endpoint local) = 0;
    virtual Result<std::optional<PortOwner>> udp_owner(Port port) = 0;
};

// Fails with platform.not_supported where the OS cannot answer.
class ILoopbackPeerInspector {
public:
    virtual ~ILoopbackPeerInspector() = default;
    virtual Result<std::optional<u32>> peer_uid(Endpoint local, Endpoint remote) = 0;
};

// `done` runs on an I/O or worker thread.
class IResolver {
public:
    virtual ~IResolver() = default;
    virtual void resolve(std::string host, CancelToken token, UniqueFunction<void(Result<std::vector<IpAddress>>)> done) = 0;
};

struct HttpHeader {
    std::string name;
    std::string value;
};

// Fails the transfer when fewer than `bytes_per_s` arrive over `window`.
struct StallPolicy {
    u64 bytes_per_s = 0;
    std::chrono::milliseconds window{0};
};

// Verification is always on; `ca_bundle` replaces the system store only where the OS has none.
struct TlsPolicy {
    std::optional<NativePath> ca_bundle;
};

struct HttpRequest {
    std::string method;
    std::string url;
    std::vector<HttpHeader> headers;
    std::vector<u8> body;
    std::chrono::milliseconds connect_timeout{0};
    std::chrono::milliseconds total_timeout{0};
    std::optional<StallPolicy> stall;
    TlsPolicy tls;
};

struct HttpStatus {
    u32 code = 0;
};

// Callbacks run on the transport's thread; on_body_chunk returning false aborts.
struct HttpCallbacks {
    UniqueFunction<void(HttpStatus, const std::vector<HttpHeader>&)> on_headers;
    UniqueFunction<bool(std::span<const u8>)> on_body_chunk;
    UniqueFunction<void(Result<HttpStatus>)> on_done;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual void perform(HttpRequest request, HttpCallbacks callbacks, CancelToken token) = 0;
};

struct QuicConnectOptions {
    std::string host;
    Port port;
    std::string alpn;
    bool ipv4_only = false;
    std::optional<NativePath> ca_bundle;
};

// Callbacks run on MsQuic threads and must only post to the strand.
struct QuicCallbacks {
    UniqueFunction<void()> on_connected;
    // Peer-opened and our own streams, in order per stream.
    UniqueFunction<void(u64 stream_id, std::span<const u8> data, bool fin)> on_stream_data;
    UniqueFunction<void(std::span<const u8>)> on_datagram;
    UniqueFunction<void(std::optional<Diagnostic>)> on_closed;
};

class IQuicConnection {
public:
    virtual ~IQuicConnection() = default;

    // Opens a bidirectional stream; the first one is the rbsb control stream.
    virtual Result<u64> open_stream() = 0;
    virtual Result<void> send(u64 stream_id, std::vector<u8> bytes, bool fin) = 0;
    virtual Result<void> send_datagram(std::vector<u8> bytes) = 0;
    virtual void close(u64 app_error) = 0;
};

class IQuicTransport {
public:
    virtual ~IQuicTransport() = default;
    virtual Result<std::unique_ptr<IQuicConnection>> open_connection(const QuicConnectOptions& options,
                                                                     QuicCallbacks callbacks) = 0;
};

}  // namespace reboot::ports
