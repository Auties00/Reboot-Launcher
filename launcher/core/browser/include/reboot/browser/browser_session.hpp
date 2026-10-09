#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "reboot/browser/connection_state.hpp"
#include "reboot/browser/rbsb_endpoint.hpp"
#include "reboot/browser/rbsb_request_error.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "wire/messages.hpp"

namespace rb {
class EventBus;
class Executor;
class IClock;
class IRandom;
class TimerService;
}  // namespace rb

namespace rb::ports {
class IQuicTransport;
}

namespace rb::net {
class AddressResolver;
class HttpClient;
}  // namespace rb::net

namespace rb::browser {

class BrowserSession;

// The session connects only while a lease is held; releasing the last one closes the connection.
class BrowserLease {
public:
    BrowserLease() = default;
    BrowserLease(BrowserLease&& other) noexcept;
    BrowserLease& operator=(BrowserLease&& other) noexcept;
    BrowserLease(const BrowserLease&) = delete;
    BrowserLease& operator=(const BrowserLease&) = delete;
    ~BrowserLease();

    void release();
    [[nodiscard]] bool held() const noexcept { return session_ != nullptr; }

private:
    friend class BrowserSession;
    BrowserLease(BrowserSession& session, u64 id) : session_(&session), id_(id) {}

    BrowserSession* session_ = nullptr;
    u64 id_ = 0;
};

// What the edge granted in Welcome. `clock_offset` is edge time minus local system time, so
// created_ms displays without trusting either clock alone.
struct EdgeSession {
    u64 edge_id = 0;
    u64 features = 0;
    std::chrono::milliseconds clock_offset{0};
    sb::wire::Limits limits;
};

// Run on the strand. After on_lost the view's id and handles are void: the subscription is
// replayed with a new sub_id once connected again, which yields a fresh SubOpen and Snapshot.
struct ViewStreamCallbacks {
    UniqueFunction<void(const sb::wire::SubOpen&)> on_open;
    UniqueFunction<void(const sb::wire::Snapshot&)> on_snapshot;
    UniqueFunction<void(const sb::wire::Delta&)> on_delta;
    UniqueFunction<void()> on_lost;
    UniqueFunction<void(const RbsbRequestError&)> on_rejected;
};

// Unsubscribes on destruction.
class ViewSubscription {
public:
    ViewSubscription() = default;
    ViewSubscription(ViewSubscription&& other) noexcept;
    ViewSubscription& operator=(ViewSubscription&& other) noexcept;
    ViewSubscription(const ViewSubscription&) = delete;
    ViewSubscription& operator=(const ViewSubscription&) = delete;
    ~ViewSubscription();

    void reset();

private:
    friend class BrowserSession;
    ViewSubscription(BrowserSession& session, u64 id) : session_(&session), id_(id) {}

    BrowserSession* session_ = nullptr;
    u64 id_ = 0;
};

template <class T>
using RbsbResult = std::expected<T, RbsbRequestError>;

struct BrowserSessionOptions {
    std::string client_version;
    // Browse connections only; the edge already pings hosts.
    std::chrono::milliseconds keepalive = std::chrono::seconds{20};
    std::chrono::milliseconds connect_timeout = std::chrono::seconds{10};
    // A request issued while disconnected waits this long for Connected.
    std::chrono::milliseconds request_wait = std::chrono::seconds{15};
    std::chrono::milliseconds request_timeout = std::chrono::seconds{10};
    // A known HTTPS URL on another host, which tells Offline from ServiceDown.
    std::string connectivity_url;
};

struct BrowserSessionDeps {
    ports::IQuicTransport& quic;
    net::AddressResolver& resolver;
    net::HttpClient& http;
    Executor& strand;
    TimerService& timers;
    const IClock& clock;
    IRandom& random;
    EventBus& events;
};

// Capabilities: server-browser.legacy-client, server-browser.+25, server-browser.+62, server-browser.+78.
// Strand-only. The one rbsb/1 browse connection, retried on FullJitterBackoff and moved after a GoAway.
// Hello offers datagrams only: a snapshot is one window of at most 200 rows, too small for zstd to pay off.
class BrowserSession {
public:
    BrowserSession(BrowserSessionDeps deps, RbsbEndpoint endpoint, BrowserSessionOptions options);
    ~BrowserSession();
    BrowserSession(const BrowserSession&) = delete;
    BrowserSession& operator=(const BrowserSession&) = delete;

    [[nodiscard]] BrowserLease acquire();

    [[nodiscard]] const ConnectionStatus& status() const noexcept;
    // Set while Connected or Draining.
    [[nodiscard]] const std::optional<EdgeSession>& edge() const noexcept;

    // A manifest or expert change; an open connection moves at once.
    void set_endpoint(RbsbEndpoint endpoint);
    [[nodiscard]] const RbsbEndpoint& endpoint() const noexcept;

    // Fails synchronously with browser.too_many_views beyond Welcome.limits.max_subscriptions.
    [[nodiscard]] Result<ViewSubscription> subscribe(const sb::wire::ViewSpec& view, u32 window,
                                                     ViewStreamCallbacks callbacks);

    // Each waits up to request_wait for Connected, then up to request_timeout for the answer.
    // req_id is assigned here. `done` runs on the strand exactly once.
    void query(sb::wire::Query query, CancelToken token, UniqueFunction<void(RbsbResult<sb::wire::QueryResult>)> done);
    void resolve(ServerId id, CancelToken token, UniqueFunction<void(RbsbResult<sb::wire::ResolveResult>)> done);
    // The password goes only into the Join frame; the frame buffer is wiped after sending.
    void join(ServerId id, std::optional<SecretString> password, CancelToken token,
              UniqueFunction<void(RbsbResult<sb::wire::JoinGrant>)> done);

private:
    friend class BrowserLease;
    friend class ViewSubscription;
    void release_lease(u64 id);
    void unsubscribe(u64 id);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::browser
