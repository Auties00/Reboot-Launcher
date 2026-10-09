#include "reboot/publish/host_publisher.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "field_checks.hpp"
#include "messages.hpp"
#include "rejected_field.hpp"
#include "reboot/browser/full_jitter_backoff.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/ports/net.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/publish/identity_hold.hpp"
#include "reboot/publish/publish_notice_sink.hpp"
#include "reboot/publish/publish_state_changed.hpp"
#include "wire/frame.hpp"
#include "wire/messages.hpp"

namespace rb::publish {

namespace {

namespace wire = sb::wire;

// Host answers are a few dozen bytes; anything near this is a broken edge.
constexpr std::size_t kControlFrameCap = std::size_t{64} << 10;
constexpr std::size_t kDatagramFrameCap = std::size_t{1} << 11;
// Used when an edge asks to retry without saying when.
constexpr std::chrono::milliseconds kMinEdgeRetry{100};

// Bits of Publication::dirty and in_flight: fields a HostUpdate has to carry.
constexpr u8 kName = 1u << 0;
constexpr u8 kDescription = 1u << 1;
constexpr u8 kMaxPlayers = 1u << 2;
constexpr u8 kHidden = 1u << 3;
constexpr u8 kGamePort = 1u << 4;
constexpr u8 kPassword = 1u << 5;

[[nodiscard]] std::string session_text(const SessionId& session) { return format_uuid(session.value); }

[[nodiscard]] Diagnostic not_published(const SessionId& session) {
    return make_diag(ErrorDomain::Publish, msg::kNotPublished).kind(ErrorKind::NotFound).arg("session", session_text(session));
}

[[nodiscard]] std::optional<IpAddress> observed_address(std::span<const u8> bytes) {
    IpAddress address;
    if (bytes.size() == 16) {
        std::copy(bytes.begin(), bytes.end(), address.bytes.begin());
        return address;
    }
    if (bytes.size() == 4)
        return IpAddress::v4((u32{bytes[0]} << 24) | (u32{bytes[1]} << 16) | (u32{bytes[2]} << 8) | u32{bytes[3]});
    return std::nullopt;
}

[[nodiscard]] std::chrono::milliseconds edge_retry(const wire::Error& error) {
    return std::max(kMinEdgeRetry, std::chrono::milliseconds{error.retry_after_ms});
}

void wipe(std::string& text) noexcept {
    text.resize(text.capacity());
    secure_wipe(text.data(), text.size());
    text.clear();
}

}  // namespace

struct HostPublisher::Impl {
    struct Alive {
        Impl* impl = nullptr;
    };

    struct Publication {
        Publication(SessionId session_in, IdentityHold hold_in, IRandom& random)
            : session(session_in), hold(std::move(hold_in)), backoff(random) {}

        SessionId session;
        IdentityHold hold;
        HostMetadata metadata;
        std::optional<SecretString> password;
        Listing listing = Listing::Unlisted;
        Port game_port;
        u32 players = 0;
        bool restarting = false;

        PublishState state;
        ReachabilityChanged reach;
        browser::FullJitterBackoff backoff;

        // Bumped whenever the connection changes, so callbacks of an abandoned one are dropped.
        u64 gen = 0;
        std::unique_ptr<ports::IQuicConnection> connection;
        std::optional<u64> control;
        std::optional<wire::StreamFramer> framer;
        bool welcomed = false;
        bool datagrams = false;
        wire::Limits limits;
        // After GoAway: the connection stays up until the reconnect, but nothing from it counts.
        bool draining = false;
        bool registered = false;
        // One rotation per connection; a second UNAUTHORIZED is not about ownership.
        bool rotated = false;
        u32 next_req = 1;
        u32 register_req = 0;
        u32 update_req = 0;
        u32 unregister_req = 0;
        u8 dirty = 0;
        u8 in_flight = 0;
        u32 players_sent = 0;
        u32 heartbeat_seq = 0;
        std::chrono::milliseconds heartbeat_every{0};

        TimerHandle connect_timer;
        TimerHandle retry_timer;
        TimerHandle register_timer;
        TimerHandle heartbeat_timer;
        TimerHandle debounce_timer;
        // Pacing within Welcome.limits, and RATE_LIMITED's hold.
        TimerHandle update_timer;
        TimerHandle unregister_timer;

        bool withdrawing = false;
        std::vector<UniqueFunction<void()>> withdrawn;
    };

    Impl(ports::IQuicTransport& quic_in, HostIdentityStore& identities_in, IPublishNoticeSink& notices_in,
         Executor& strand_in, TimerService& timers_in, IRandom& random_in, EventBus& events_in,
         browser::RbsbEndpoint edge_in)
        : quic(quic_in), identities(identities_in), notices(notices_in), strand(strand_in), timers(timers_in),
          random(random_in), events(events_in), edge(std::move(edge_in)), alive(std::make_shared<Alive>(Alive{this})) {}

    ~Impl() {
        alive->impl = nullptr;
        for (auto& [session, pub] : pubs)
            if (pub->connection) pub->connection->close(0);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] Publication* find(const SessionId& session) {
        const auto it = pubs.find(session);
        return it == pubs.end() ? nullptr : it->second.get();
    }

    [[nodiscard]] Publication* live(const SessionId& session, u64 gen) {
        Publication* pub = find(session);
        return pub != nullptr && pub->gen == gen ? pub : nullptr;
    }

    [[nodiscard]] static bool hidden_now(const Publication& pub) noexcept {
        return hidden_on_edge(pub.listing, pub.restarting);
    }

    [[nodiscard]] static u32 next_request(Publication& pub) noexcept {
        if (pub.next_req == 0) pub.next_req = 1;
        return pub.next_req++;
    }

    void publish_state(const Publication& pub) {
        events.publish(EventKind::PublishStateChanged, PublishStateChanged{pub.session, pub.state},
                       EventScope{.session = pub.session, .op = std::nullopt, .coalesce_key = session_text(pub.session)});
    }

    void publish_reach(const Publication& pub) {
        events.publish(EventKind::ReachabilityChanged, pub.reach,
                       EventScope{.session = pub.session, .op = std::nullopt, .coalesce_key = session_text(pub.session)});
    }

    void notice(const Publication& pub, PublishNoticeKind kind, Diagnostic message) {
        notices.on_publish_notice(PublishNotice{kind, pub.session, pub.hold.profile(), std::move(message)});
    }

    void set_phase(Publication& pub, PublishPhase phase, std::optional<Diagnostic> error) {
        pub.state.phase = phase;
        pub.state.error = std::move(error);
        publish_state(pub);
    }

    void close_connection(Publication& pub) {
        if (pub.connection) pub.connection->close(0);
        pub.connection.reset();
        pub.control.reset();
        pub.framer.reset();
        pub.welcomed = false;
        pub.datagrams = false;
        pub.draining = false;
        pub.registered = false;
        pub.rotated = false;
        pub.register_req = 0;
        pub.update_req = 0;
        pub.unregister_req = 0;
        pub.in_flight = 0;
        pub.connect_timer.cancel();
        pub.register_timer.cancel();
        pub.heartbeat_timer.cancel();
        pub.debounce_timer.cancel();
        pub.update_timer.cancel();
        ++pub.gen;
    }

    // Ends what the edge knows of this session: the reachability goes blank and the connection closes.
    void end_registration(Publication& pub) {
        const bool had_reach = pub.reach.status || pub.reach.public_endpoint;
        pub.reach.status.reset();
        pub.reach.probe_failures = 0;
        pub.reach.public_endpoint.reset();
        close_connection(pub);
        if (had_reach) publish_reach(pub);
    }

    void connect(Publication& pub) {
        close_connection(pub);
        pub.retry_timer.cancel();
        if (pub.state.phase != PublishPhase::Connecting) set_phase(pub, PublishPhase::Connecting, pub.state.error);
        ports::QuicConnectOptions options;
        options.host = edge.host;
        options.port = edge.port;
        options.alpn = std::string(wire::kAlpn);
        // The entry's address is the connection's, and the probe and joins are IPv4.
        options.ipv4_only = true;
        options.ca_bundle = edge.ca_bundle;
        Result<std::unique_ptr<ports::IQuicConnection>> opened = quic.open_connection(options, callbacks(pub.session, pub.gen));
        if (!opened) {
            retry_later(pub, unreachable(std::move(opened.error())));
            return;
        }
        pub.connection = std::move(*opened);
        pub.connect_timer = timers.after(default_deadline(OpKind::QuicConnect), [weak = alive, session = pub.session, gen = pub.gen] {
            if (Impl* self = weak->impl)
                if (Publication* target = self->live(session, gen)) self->retry_later(*target, self->unreachable(std::nullopt));
        });
    }

    [[nodiscard]] ports::QuicCallbacks callbacks(const SessionId& session, u64 gen) {
        // MsQuic calls these on its own threads; everything is copied and posted to the strand.
        Executor* target = &strand;
        ports::QuicCallbacks out;
        out.on_connected = [weak = alive, target, session, gen] {
            target->post([weak, session, gen] {
                if (Impl* self = weak->impl) self->on_connected(session, gen);
            });
        };
        out.on_stream_data = [weak = alive, target, session, gen](u64 stream, std::span<const u8> data, bool) {
            target->post([weak, session, gen, stream, bytes = std::vector<u8>(data.begin(), data.end())] {
                if (Impl* self = weak->impl) self->on_stream_data(session, gen, stream, bytes);
            });
        };
        out.on_datagram = [weak = alive, target, session, gen](std::span<const u8> data) {
            target->post([weak, session, gen, bytes = std::vector<u8>(data.begin(), data.end())] {
                if (Impl* self = weak->impl) self->on_datagram(session, gen, bytes);
            });
        };
        out.on_closed = [weak = alive, target, session, gen](std::optional<Diagnostic> error) {
            target->post([weak, session, gen, error = std::move(error)]() mutable {
                if (Impl* self = weak->impl) self->on_closed(session, gen, std::move(error));
            });
        };
        return out;
    }

    [[nodiscard]] Diagnostic unreachable(std::optional<Diagnostic> cause) const {
        DiagBuilder builder = make_diag(ErrorDomain::Publish, msg::kEdgeUnreachable).arg("host", edge.host).retryable();
        if (cause) std::move(builder).cause(std::move(*cause));
        return std::move(builder).build();
    }

    void retry_later(Publication& pub, Diagnostic error, std::chrono::milliseconds floor = {}) {
        end_registration(pub);
        const std::chrono::milliseconds delay = std::max(floor, pub.backoff.next());
        set_phase(pub, PublishPhase::Retrying, std::move(error));
        pub.retry_timer = timers.after(delay, [weak = alive, session = pub.session] {
            if (Impl* self = weak->impl)
                if (Publication* target = self->find(session); target != nullptr && !target->withdrawing) self->connect(*target);
        });
    }

    void on_connected(const SessionId& session, u64 gen) {
        Publication* pub = live(session, gen);
        if (pub == nullptr || pub->control) return;
        Result<u64> stream = pub->connection->open_stream();
        if (!stream) {
            retry_later(*pub, unreachable(std::move(stream.error())));
            return;
        }
        pub->control = *stream;
        pub->framer.emplace(kControlFrameCap);
        const wire::Hello hello{wire::Role::host, wire::kProtoMinor, {}, wire::feature::datagrams};
        if (Result<void> sent = pub->connection->send(*pub->control, wire::frame_bytes(hello), false); !sent)
            retry_later(*pub, unreachable(std::move(sent.error())));
    }

    void on_closed(const SessionId& session, u64 gen, std::optional<Diagnostic> error) {
        Publication* pub = live(session, gen);
        if (pub == nullptr) return;
        if (pub->withdrawing) {
            finish_withdraw(session);
            return;
        }
        // The GoAway delay already decides when to come back.
        if (pub->draining) {
            close_connection(*pub);
            return;
        }
        if (!pub->welcomed) {
            retry_later(*pub, unreachable(std::move(error)));
            return;
        }
        connection_lost(*pub, std::move(error));
    }

    void connection_lost(Publication& pub, std::optional<Diagnostic> cause) {
        DiagBuilder builder = make_diag(ErrorDomain::Publish, msg::kConnectionLost).arg("host", edge.host).retryable();
        if (cause) std::move(builder).cause(std::move(*cause));
        retry_later(pub, std::move(builder).build());
    }

    void on_stream_data(const SessionId& session, u64 gen, u64 stream, const std::vector<u8>& bytes) {
        Publication* pub = live(session, gen);
        if (pub == nullptr || !pub->control || stream != *pub->control) return;
        std::vector<std::pair<wire::FrameType, std::vector<u8>>> frames;
        const auto status = pub->framer->feed(bytes, [&](const wire::FrameView& frame) {
            frames.emplace_back(frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end()));
            return true;
        });
        if (status != wire::StreamFramer::Status::ok) {
            if (!pub->draining && !pub->withdrawing) connection_lost(*pub, std::nullopt);
            return;
        }
        // A frame may end the connection or the publication; the rest of its frames then go with it.
        for (auto& [type, payload] : frames) {
            Publication* current = live(session, gen);
            if (current == nullptr) return;
            handle_frame(*current, type, payload);
        }
    }

    void on_datagram(const SessionId& session, u64 gen, const std::vector<u8>& bytes) {
        std::vector<std::vector<u8>> statuses;
        (void)wire::for_each_frame(bytes, kDatagramFrameCap, [&](const wire::FrameView& frame) {
            if (frame.type == wire::FrameType::host_status) statuses.emplace_back(frame.payload.begin(), frame.payload.end());
            return true;
        });
        for (const auto& payload : statuses) {
            Publication* pub = live(session, gen);
            if (pub == nullptr) return;
            handle_frame(*pub, wire::FrameType::host_status, payload);
        }
    }

    void handle_frame(Publication& pub, wire::FrameType type, std::span<const u8> payload) {
        if (pub.draining) return;
        // Only the answer to HostUnregister still matters.
        if (pub.withdrawing) {
            wire::Ack ack;
            wire::Error error;
            if ((type == wire::FrameType::ack && wire::decode(payload, ack) && ack.req_id == pub.unregister_req) ||
                (type == wire::FrameType::error && wire::decode(payload, error) && error.req_id == pub.unregister_req))
                finish_withdraw(pub.session);
            return;
        }
        if (!pub.welcomed) {
            wire::Welcome welcome;
            if (type == wire::FrameType::welcome && wire::decode(payload, welcome)) on_welcome(pub, welcome);
            return;
        }
        switch (type) {
            case wire::FrameType::host_registered: {
                wire::HostRegistered registered;
                if (wire::decode(payload, registered)) on_registered(pub, registered);
                if (registered.token) secure_wipe(registered.token->data(), registered.token->size());
                return;
            }
            case wire::FrameType::ack: {
                wire::Ack ack;
                if (wire::decode(payload, ack)) on_ack(pub, ack.req_id);
                return;
            }
            case wire::FrameType::error: {
                wire::Error error;
                if (wire::decode(payload, error)) on_error(pub, error);
                return;
            }
            case wire::FrameType::go_away: {
                wire::GoAway go_away;
                if (wire::decode(payload, go_away)) on_go_away(pub, go_away);
                return;
            }
            case wire::FrameType::host_status: {
                wire::HostStatus status;
                if (wire::decode(payload, status)) on_status(pub, status);
                return;
            }
            default: return;
        }
    }

    void on_welcome(Publication& pub, const wire::Welcome& welcome) {
        pub.connect_timer.cancel();
        pub.welcomed = true;
        pub.datagrams = (welcome.features & wire::feature::datagrams) != 0;
        pub.limits = welcome.limits;
        send_register(pub);
    }

    void send_register(Publication& pub) {
        pub.register_timer.cancel();
        const HostIdentity& identity = pub.hold.identity();
        wire::HostRegister message;
        message.req_id = next_request(pub);
        message.id = identity.server.value;
        if (identity.token) message.token = identity.token->reveal();
        message.name = pub.metadata.name;
        message.description = pub.metadata.description;
        message.version = pub.metadata.version.canonical();
        message.author = pub.metadata.author;
        message.game_port = pub.game_port.value;
        if (pub.password) message.password = pub.password->reveal();
        message.max_players = pub.metadata.max_players;
        message.hidden = hidden_now(pub);
        message.players = pub.players;
        std::vector<u8> bytes = wire::frame_bytes(message);
        if (message.token) secure_wipe(message.token->data(), message.token->size());
        if (message.password) wipe(*message.password);

        pub.register_req = message.req_id;
        // The registration carries every current value, so nothing is left to update.
        pub.dirty = 0;
        pub.in_flight = 0;
        pub.update_req = 0;
        pub.debounce_timer.cancel();
        pub.players_sent = pub.players;
        pub.state.server = identity.server;
        pub.state.hidden = message.hidden;
        if (Result<void> sent = pub.connection->send(*pub.control, std::move(bytes), false); !sent) {
            connection_lost(pub, std::move(sent.error()));
            return;
        }
        set_phase(pub, PublishPhase::Registering, std::nullopt);
    }

    void on_registered(Publication& pub, wire::HostRegistered& registered) {
        if (registered.req_id == 0 || registered.req_id != pub.register_req) return;
        pub.register_req = 0;
        pub.registered = true;
        pub.backoff.reset();
        if (registered.token) save_token(pub, HostToken(*registered.token));

        const u32 heartbeat_ms = registered.heartbeat_ms != 0 ? registered.heartbeat_ms : pub.limits.heartbeat_ms;
        pub.heartbeat_every = std::chrono::milliseconds{heartbeat_ms / 2};
        arm_heartbeat(pub);

        pub.reach.status = HostStatus::AwaitingProbe;
        pub.reach.probe_failures = 0;
        if (const std::optional<IpAddress> address = observed_address(registered.observed_address))
            pub.reach.public_endpoint = Endpoint{*address, pub.game_port};
        else
            pub.reach.public_endpoint.reset();
        set_phase(pub, PublishPhase::Registered, std::nullopt);
        publish_reach(pub);

        // Edits made while the registration was in flight.
        if (pub.players != pub.players_sent) send_players(pub);
        flush_updates(pub);
    }

    void save_token(Publication& pub, HostToken token) {
        const ServerId server = pub.hold.identity().server;
        identities.save_token(pub.hold, std::move(token),
                              [weak = alive, session = pub.session, profile = pub.hold.profile(), server](Result<void> saved) {
                                  Impl* self = weak->impl;
                                  if (self == nullptr || saved) return;
                                  self->notices.on_publish_notice(PublishNotice{
                                      PublishNoticeKind::TokenNotSaved, session, profile,
                                      make_diag(ErrorDomain::Publish, msg::kTokenNotSaved)
                                          .severity(Severity::Warning)
                                          .arg("server", format_uuid(server.value))
                                          .cause(std::move(saved.error()))
                                          .build()});
                              });
    }

    void arm_heartbeat(Publication& pub) {
        if (pub.heartbeat_every.count() <= 0) return;
        pub.heartbeat_timer = timers.after(pub.heartbeat_every, [weak = alive, session = pub.session, gen = pub.gen] {
            if (Impl* self = weak->impl)
                if (Publication* target = self->live(session, gen)) self->heartbeat(*target);
        });
    }

    void heartbeat(Publication& pub) {
        if (!pub.connection) return;
        const wire::HostHeartbeat beat{pub.heartbeat_seq++};
        Result<void> sent = std::unexpected(Diagnostic{});
        if (pub.datagrams) sent = pub.connection->send_datagram(wire::frame_bytes(beat, wire::LenWidth::two));
        // The edge also takes heartbeats on the control stream.
        if (!sent) (void)pub.connection->send(*pub.control, wire::frame_bytes(beat), false);
        arm_heartbeat(pub);
    }

    void on_status(Publication& pub, const wire::HostStatus& status) {
        if (!pub.registered) return;
        pub.reach.status = host_status(status.reachable, status.probe_failures);
        pub.reach.probe_failures = status.probe_failures;
        publish_reach(pub);
    }

    void on_go_away(Publication& pub, const wire::GoAway& go_away) {
        const std::chrono::milliseconds floor =
            browser::go_away_delay(random, std::chrono::milliseconds{go_away.reconnect_after_ms});
        const std::chrono::milliseconds delay = std::max(floor, pub.backoff.next());
        const bool had_reach = pub.reach.status || pub.reach.public_endpoint;
        pub.reach.status.reset();
        pub.reach.probe_failures = 0;
        pub.reach.public_endpoint.reset();
        pub.draining = true;
        pub.registered = false;
        pub.register_req = 0;
        pub.update_req = 0;
        pub.in_flight = 0;
        pub.register_timer.cancel();
        pub.debounce_timer.cancel();
        pub.update_timer.cancel();
        if (had_reach) publish_reach(pub);
        set_phase(pub, PublishPhase::Retrying,
                  make_diag(ErrorDomain::Publish, msg::kEdgeGoAway).arg("delay", delay).retryable().build());
        pub.retry_timer = timers.after(delay, [weak = alive, session = pub.session] {
            if (Impl* self = weak->impl)
                if (Publication* target = self->find(session); target != nullptr && !target->withdrawing) self->connect(*target);
        });
    }

    void on_error(Publication& pub, const wire::Error& error) {
        REBOOT_LOG_DEBUG(Host, "rbsb edge error {} for request {} of session {}", static_cast<u32>(error.code),
                         error.req_id, session_text(pub.session));
        if (error.req_id != 0 && error.req_id == pub.register_req) return register_failed(pub, error);
        if (error.req_id != 0 && error.req_id == pub.update_req) return update_failed(pub, error);
        if (error.req_id != 0 && error.req_id == pub.unregister_req) return finish_withdraw(pub.session);
        if (error.req_id != 0) return;
        if (error.code == wire::ErrorCode::conflict && pub.registered) return supersede(pub);
        if (error.code == wire::ErrorCode::unauthorized) return refuse(pub, not_allowed());
    }

    [[nodiscard]] static Diagnostic not_allowed() {
        return make_diag(ErrorDomain::Publish, msg::kEdgeNotAllowed).build();
    }

    void register_failed(Publication& pub, const wire::Error& error) {
        pub.register_req = 0;
        using wire::ErrorCode;
        switch (error.code) {
            case ErrorCode::unauthorized:
                if (pub.rotated) return refuse(pub, not_allowed());
                return rotate(pub);
            case ErrorCode::conflict: return register_again(pub, edge_retry(error));
            case ErrorCode::rate_limited: {
                const std::chrono::milliseconds delay = edge_retry(error);
                set_phase(pub, PublishPhase::Retrying,
                          make_diag(ErrorDomain::Publish, msg::kEdgeRateLimited).arg("delay", delay).retryable().build());
                return register_again(pub, delay);
            }
            case ErrorCode::bad_request: return refuse(pub, edge_rejected(error.message));
            case ErrorCode::limit_exceeded:
                return refuse(pub, make_diag(ErrorDomain::Publish, msg::kEdgeHostLimit).kind(ErrorKind::Conflict).build());
            case ErrorCode::unsupported: return refuse(pub, not_allowed());
            default:
                return retry_later(pub, make_diag(ErrorDomain::Publish, msg::kEdgeUnavailable).retryable().build(),
                                   std::chrono::milliseconds{error.retry_after_ms});
        }
    }

    void register_again(Publication& pub, std::chrono::milliseconds delay) {
        pub.register_timer = timers.after(delay, [weak = alive, session = pub.session, gen = pub.gen] {
            if (Impl* self = weak->impl)
                if (Publication* target = self->live(session, gen); target != nullptr && !target->withdrawing) self->send_register(*target);
        });
    }

    void rotate(Publication& pub) {
        const ServerId old_server = pub.hold.identity().server;
        const ServerId new_server = identities.rotate(pub.hold, [session = pub.session](Result<void> saved) {
            if (!saved)
                REBOOT_LOG_WARN(Host, "The new server id of session {} could not be saved ({})", session_text(session),
                                saved.error().id);
        });
        pub.rotated = true;
        pub.state.server = new_server;
        notice(pub, PublishNoticeKind::IdentityRotated,
               make_diag(ErrorDomain::Publish, msg::kIdentityRotated)
                   .severity(Severity::Warning)
                   .arg("old_server", format_uuid(old_server.value))
                   .arg("new_server", format_uuid(new_server.value))
                   .build());
        send_register(pub);
    }

    void update_failed(Publication& pub, const wire::Error& error) {
        const u8 fields = pub.in_flight;
        pub.update_req = 0;
        pub.in_flight = 0;
        using wire::ErrorCode;
        switch (error.code) {
            case ErrorCode::rate_limited:
            case ErrorCode::conflict:
            case ErrorCode::unavailable:
            case ErrorCode::internal:
            case ErrorCode::unknown:
                // The fields go out again with whatever values they have by then.
                pub.dirty |= fields;
                pub.update_timer = timers.after(edge_retry(error), [weak = alive, session = pub.session, gen = pub.gen] {
                    if (Impl* self = weak->impl)
                        if (Publication* target = self->live(session, gen)) self->flush_updates(*target);
                });
                return;
            case ErrorCode::bad_request: return refuse(pub, edge_rejected(error.message));
            default: return refuse(pub, not_allowed());
        }
    }

    void refuse(Publication& pub, Diagnostic error) {
        end_registration(pub);
        pub.retry_timer.cancel();
        set_phase(pub, PublishPhase::Refused, std::move(error));
    }

    void supersede(Publication& pub) {
        end_registration(pub);
        pub.retry_timer.cancel();
        Diagnostic message = make_diag(ErrorDomain::Publish, msg::kHostedElsewhere)
                                 .severity(Severity::Warning)
                                 .kind(ErrorKind::Conflict)
                                 .arg("server", format_uuid(pub.state.server.value))
                                 .build();
        set_phase(pub, PublishPhase::Superseded, message);
        notice(pub, PublishNoticeKind::HostedElsewhere, std::move(message));
    }

    void edited(Publication& pub, bool urgent) {
        if (urgent) {
            flush_updates(pub);
            return;
        }
        if (pub.dirty == 0 || pub.debounce_timer.active()) return;
        pub.debounce_timer = timers.after(kUpdateDebounce, [weak = alive, session = pub.session] {
            if (Impl* self = weak->impl)
                if (Publication* target = self->find(session)) self->flush_updates(*target);
        });
    }

    void mark_hidden(Publication& pub) {
        if (hidden_now(pub) != pub.state.hidden) pub.dirty |= kHidden;
        else pub.dirty &= static_cast<u8>(~kHidden);
    }

    void flush_updates(Publication& pub) {
        if (!pub.registered || pub.dirty == 0 || pub.update_req != 0 || pub.update_timer.active()) return;
        pub.debounce_timer.cancel();
        wire::HostUpdate update;
        update.req_id = next_request(pub);
        const u8 fields = pub.dirty;
        if ((fields & kName) != 0) update.name = pub.metadata.name;
        if ((fields & kDescription) != 0) update.description = pub.metadata.description;
        if ((fields & kMaxPlayers) != 0) update.max_players = pub.metadata.max_players;
        if ((fields & kHidden) != 0) update.hidden = hidden_now(pub);
        if ((fields & kGamePort) != 0) update.game_port = pub.game_port.value;
        // An empty password removes it.
        if ((fields & kPassword) != 0) update.password = pub.password ? pub.password->reveal() : std::string{};
        std::vector<u8> bytes = wire::frame_bytes(update);
        if (update.password) wipe(*update.password);

        pub.dirty = 0;
        pub.in_flight = fields;
        pub.update_req = update.req_id;
        if (update.hidden) pub.state.hidden = *update.hidden;
        if (Result<void> sent = pub.connection->send(*pub.control, std::move(bytes), false); !sent) {
            connection_lost(pub, std::move(sent.error()));
            return;
        }
        if (update.hidden) publish_state(pub);
        if (pub.limits.host_update_per_sec != 0) {
            pub.update_timer = timers.after(std::chrono::milliseconds{1000 / pub.limits.host_update_per_sec},
                                            [weak = alive, session = pub.session, gen = pub.gen] {
                                                if (Impl* self = weak->impl)
                                                    if (Publication* target = self->live(session, gen)) self->flush_updates(*target);
                                            });
        }
    }

    void on_ack(Publication& pub, u32 req_id) {
        if (req_id == 0) return;
        if (req_id == pub.unregister_req) return finish_withdraw(pub.session);
        if (req_id != pub.update_req) return;
        pub.update_req = 0;
        pub.in_flight = 0;
        flush_updates(pub);
    }

    void send_players(Publication& pub) {
        wire::HostUpdate update;
        update.players = pub.players;
        pub.players_sent = pub.players;
        if (Result<void> sent = pub.connection->send(*pub.control, wire::frame_bytes(update), false); !sent)
            connection_lost(pub, std::move(sent.error()));
    }

    void withdraw(const SessionId& session, UniqueFunction<void()> done) {
        Publication* pub = find(session);
        if (pub == nullptr) {
            if (done) done();
            return;
        }
        if (done) pub->withdrawn.push_back(std::move(done));
        if (pub->withdrawing) return;
        pub->withdrawing = true;
        pub->retry_timer.cancel();
        pub->register_timer.cancel();
        pub->debounce_timer.cancel();
        pub->update_timer.cancel();
        set_phase(*pub, PublishPhase::Withdrawing, std::nullopt);
        if (!pub->registered || pub->draining) {
            finish_withdraw(session);
            return;
        }
        pub->unregister_req = next_request(*pub);
        if (Result<void> sent = pub->connection->send(*pub->control, wire::frame_bytes(wire::HostUnregister{pub->unregister_req}), false);
            !sent) {
            finish_withdraw(session);
            return;
        }
        pub->unregister_timer = timers.after(kUnregisterAckWait, [weak = alive, session] {
            if (Impl* self = weak->impl) self->finish_withdraw(session);
        });
    }

    void finish_withdraw(const SessionId& session) {
        const auto it = pubs.find(session);
        if (it == pubs.end()) return;
        std::unique_ptr<Publication> pub = std::move(it->second);
        pubs.erase(it);
        end_registration(*pub);
        std::vector<UniqueFunction<void()>> withdrawn = std::move(pub->withdrawn);
        // The identity is free before anyone hears the publication ended.
        pub.reset();
        for (UniqueFunction<void()>& done : withdrawn) done();
    }

    ports::IQuicTransport& quic;
    HostIdentityStore& identities;
    IPublishNoticeSink& notices;
    Executor& strand;
    TimerService& timers;
    IRandom& random;
    EventBus& events;
    browser::RbsbEndpoint edge;
    std::shared_ptr<Alive> alive;
    std::map<SessionId, std::unique_ptr<Publication>> pubs;
};

HostPublisher::HostPublisher(ports::IQuicTransport& quic, HostIdentityStore& identities, IPublishNoticeSink& notices,
                             Executor& strand, TimerService& timers, IRandom& random, EventBus& events,
                             browser::RbsbEndpoint edge)
    : impl_(std::make_unique<Impl>(quic, identities, notices, strand, timers, random, events, std::move(edge))) {}

HostPublisher::~HostPublisher() = default;

void HostPublisher::set_edge(browser::RbsbEndpoint edge) { impl_->edge = std::move(edge); }

Result<void> HostPublisher::publish(PublishRequest request) {
    Result<PublishRequest> fitted = fit_request(std::move(request));
    if (!fitted) return std::unexpected(std::move(fitted.error()));
    PublishRequest& req = *fitted;
    if (impl_->pubs.contains(req.session))
        return make_diag(ErrorDomain::Publish, msg::kAlreadyPublished)
            .kind(ErrorKind::Conflict)
            .arg("session", session_text(req.session))
            .fail();
    Result<IdentityHold> hold = impl_->identities.acquire(req.profile);
    if (!hold) return std::unexpected(std::move(hold.error()));

    auto pub = std::make_unique<Impl::Publication>(req.session, std::move(*hold), impl_->random);
    pub->metadata = std::move(req.metadata);
    if (req.password && !req.password->reveal().empty()) pub->password = std::move(req.password);
    pub->listing = req.listing;
    pub->game_port = req.game_port;
    pub->players = req.players;
    pub->state.phase = PublishPhase::Connecting;
    pub->state.server = pub->hold.identity().server;
    pub->state.hidden = Impl::hidden_now(*pub);
    pub->state.advertised_port = req.game_port;
    pub->reach.session = req.session;
    Impl::Publication& added = *impl_->pubs.emplace(req.session, std::move(pub)).first->second;
    impl_->publish_state(added);
    impl_->connect(added);
    return {};
}

Result<void> HostPublisher::update(const SessionId& session, MetadataPatch patch) {
    Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr || pub->withdrawing) return std::unexpected(not_published(session));
    Result<MetadataPatch> fitted = fit_patch(std::move(patch));
    if (!fitted) return std::unexpected(std::move(fitted.error()));
    MetadataPatch& edit = *fitted;
    if (edit.name && *edit.name != pub->metadata.name) {
        pub->metadata.name = std::move(*edit.name);
        pub->dirty |= kName;
    }
    if (edit.description && *edit.description != pub->metadata.description) {
        pub->metadata.description = std::move(*edit.description);
        pub->dirty |= kDescription;
    }
    if (edit.max_players && *edit.max_players != pub->metadata.max_players) {
        pub->metadata.max_players = *edit.max_players;
        pub->dirty |= kMaxPlayers;
    }
    if (edit.password) {
        const std::string_view before = pub->password ? std::string_view(pub->password->reveal()) : std::string_view{};
        if (before != edit.password->reveal()) {
            if (edit.password->reveal().empty()) pub->password.reset();
            else pub->password = std::move(*edit.password);
            pub->dirty |= kPassword;
        }
    }
    bool urgent = false;
    if (edit.listing) {
        pub->listing = *edit.listing;
        impl_->mark_hidden(*pub);
        urgent = (pub->dirty & kHidden) != 0;
    }
    impl_->edited(*pub, urgent);
    return {};
}

Result<void> HostPublisher::set_game_port(const SessionId& session, Port port) {
    if (Result<void> checked = check_game_port(port); !checked) return checked;
    Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr || pub->withdrawing) return std::unexpected(not_published(session));
    if (port == pub->game_port) return {};
    pub->game_port = port;
    pub->state.advertised_port = port;
    pub->dirty |= kGamePort;
    impl_->publish_state(*pub);
    if (pub->reach.public_endpoint) {
        pub->reach.public_endpoint->port = port;
        impl_->publish_reach(*pub);
    }
    impl_->edited(*pub, true);
    return {};
}

Result<void> HostPublisher::set_restarting(const SessionId& session, bool restarting) {
    Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr || pub->withdrawing) return std::unexpected(not_published(session));
    pub->restarting = restarting;
    impl_->mark_hidden(*pub);
    impl_->edited(*pub, true);
    return {};
}

Result<void> HostPublisher::set_players(const SessionId& session, u32 players) {
    if (Result<void> checked = check_player_count(players); !checked) return checked;
    Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr || pub->withdrawing) return std::unexpected(not_published(session));
    pub->players = players;
    if (pub->registered && players != pub->players_sent) impl_->send_players(*pub);
    return {};
}

void HostPublisher::withdraw(const SessionId& session, UniqueFunction<void()> done) {
    impl_->withdraw(session, std::move(done));
}

void HostPublisher::withdraw_all(UniqueFunction<void()> done) {
    std::vector<SessionId> sessions;
    sessions.reserve(impl_->pubs.size());
    for (const auto& [session, pub] : impl_->pubs) sessions.push_back(session);
    if (sessions.empty()) {
        if (done) done();
        return;
    }
    struct Countdown {
        std::size_t left = 0;
        UniqueFunction<void()> done;
    };
    auto countdown = std::make_shared<Countdown>(Countdown{sessions.size(), std::move(done)});
    for (const SessionId& session : sessions) {
        impl_->withdraw(session, [countdown] {
            if (--countdown->left == 0 && countdown->done) countdown->done();
        });
    }
}

std::optional<PublishState> HostPublisher::state(const SessionId& session) const {
    const Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr) return std::nullopt;
    return pub->state;
}

std::optional<ReachabilityChanged> HostPublisher::reachability(const SessionId& session) const {
    const Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr) return std::nullopt;
    return pub->reach;
}

bool HostPublisher::has_publications() const noexcept { return !impl_->pubs.empty(); }

Result<ShareLink> HostPublisher::share_link(const SessionId& session) const {
    const Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr) return std::unexpected(not_published(session));
    return ShareLink{pub->state.server};
}

Result<Endpoint> HostPublisher::public_endpoint(const SessionId& session) const {
    const Impl::Publication* pub = impl_->find(session);
    if (pub == nullptr) return std::unexpected(not_published(session));
    if (!pub->reach.public_endpoint)
        return make_diag(ErrorDomain::Publish, msg::kNotRegistered).arg("session", session_text(session)).fail();
    return *pub->reach.public_endpoint;
}

}  // namespace rb::publish
