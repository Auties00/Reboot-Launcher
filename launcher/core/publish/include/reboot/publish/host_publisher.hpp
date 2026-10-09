#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "reboot/browser/rbsb_endpoint.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/publish/metadata_patch.hpp"
#include "reboot/publish/publish_request.hpp"
#include "reboot/publish/publish_state.hpp"
#include "reboot/publish/reachability_changed.hpp"
#include "reboot/publish/share_link.hpp"

namespace rb {
class EventBus;
class Executor;
class IRandom;
class TimerService;
}  // namespace rb

namespace rb::ports {
class IQuicTransport;
}

namespace rb::publish {

class HostIdentityStore;
class IPublishNoticeSink;

// Metadata edits within this window go out as one HostUpdate.
inline constexpr std::chrono::milliseconds kUpdateDebounce{400};
// How long withdraw() waits for the HostUnregister Ack before closing anyway.
inline constexpr std::chrono::milliseconds kUnregisterAckWait{2000};

// Capabilities: hosting.publication, hosting.share, matchmaking-networking.public-ip, hosting.+17,
// hosting.+18, hosting.+20, hosting.+35, hosting.+63, hosting.+86.
// Strand-only; MsQuic callbacks only post here. One IPv4 rbsb/1 HOST connection per published
// session, which exists only while the session does, so a settings edit never touches the network.
// - Edge: the same browser::RbsbEndpoint the engine selects for the browser, so both use one edge.
// - Register: HostRegister{server id, token?, metadata, game_port, password, hidden}. A token in
//   HostRegistered goes to HostIdentityStore::save_token at once.
// - Heartbeat: a HostHeartbeat datagram every heartbeat_ms / 2.
// - Edits: debounced by kUpdateDebounce into one HostUpdate carrying only changed fields, with one
//   in flight and the latest winning, within Welcome.limits; RATE_LIMITED waits retry_after_ms.
//   hidden and game_port changes skip the debounce; player counts go at once with req_id 0.
// - hidden = hidden_on_edge(listing, restarting), recomputed for every message.
// - Reconnect: on a lost connection or GoAway, reconnect with browser::FullJitterBackoff
//   (go_away_delay as the floor after GoAway) to a freshly resolved edge, and register again with
//   the token.
// - Errors are matched to the session's current connection by req_id. Anything arriving on a
//   connection being replaced or closed is ignored, so the CONFLICT our own reconnect causes on
//   the abandoned connection never stops a healthy publication.
// - UNAUTHORIZED answering our pending HostRegister: HostIdentityStore::rotate, an IdentityRotated
//   notice, then register the new id. Any other UNAUTHORIZED is a role or protocol bug: the
//   session goes Refused with publish.edge_not_allowed and keeps its id.
// - CONFLICT answering our pending HostRegister retries after retry_after_ms; CONFLICT on the
//   current, registered connection means another machine registered the id, so the session goes
//   Superseded with a HostedElsewhere notice and stops publishing.
// - BAD_REQUEST names the field; it is mapped to a known field id, never shown as the edge's text.
// - An unreachable edge leaves the session running and "not listed" (Retrying).
// - Being published is the Registered phase. Nothing is discarded at startup: the edge drops an
//   entry once its connection closes (15 s grace). Discoverability is the Listing.
class HostPublisher {
public:
    HostPublisher(ports::IQuicTransport& quic, HostIdentityStore& identities, IPublishNoticeSink& notices,
                  Executor& strand, TimerService& timers, IRandom& random, EventBus& events,
                  browser::RbsbEndpoint edge);
    ~HostPublisher();
    HostPublisher(const HostPublisher&) = delete;
    HostPublisher& operator=(const HostPublisher&) = delete;

    // Used from the next connection on; the engine calls it when the manifest's endpoint changes.
    void set_edge(browser::RbsbEndpoint edge);

    // Validates synchronously with fit_request; fails with publish.already_published, or
    // publish.profile_busy when another session holds the profile's identity. Progress arrives as
    // PublishStateChanged and ReachabilityChanged.
    Result<void> publish(PublishRequest request);

    // Each fails with publish.not_published for a session that is not published.
    // Validates with fit_patch, so an edit is checked as strictly as the registration.
    Result<void> update(const SessionId& session, MetadataPatch patch);
    // Fails with publish.game_port_missing for port 0.
    Result<void> set_game_port(const SessionId& session, Port port);
    Result<void> set_restarting(const SessionId& session, bool restarting);
    // Fails with publish.player_count_too_high above kMaxPlayerLimit.
    Result<void> set_players(const SessionId& session, u32 players);

    // Sends HostUnregister, waits up to kUnregisterAckWait for the Ack, closes the connection and
    // releases the identity. `done` runs on the strand; an unknown session completes at once.
    void withdraw(const SessionId& session, UniqueFunction<void()> done);
    // ShutdownCoordinator's unpublish step.
    void withdraw_all(UniqueFunction<void()> done);

    [[nodiscard]] std::optional<PublishState> state(const SessionId& session) const;
    // The last ReachabilityChanged of a published session.
    [[nodiscard]] std::optional<ReachabilityChanged> reachability(const SessionId& session) const;
    // Keeps an on-demand engine from idling out.
    [[nodiscard]] bool has_publications() const noexcept;

    // reboot://<server id> of the session's identity; publish.not_published otherwise.
    [[nodiscard]] Result<ShareLink> share_link(const SessionId& session) const;
    // ReachabilityChanged::public_endpoint as of now. Fails with publish.not_registered until a
    // HostRegistered arrived; never asks a third party.
    [[nodiscard]] Result<Endpoint> public_endpoint(const SessionId& session) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::publish
