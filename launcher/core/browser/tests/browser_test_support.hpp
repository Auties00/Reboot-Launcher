#pragma once

#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/full_jitter_backoff.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/net/address_resolver.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_quic_peer.hpp"
#include "reboot/testing/fake_quic_transport.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_resolver.hpp"
#include "wire/frame.hpp"
#include "wire/messages.hpp"

namespace rb::browser::test {

namespace wire = sb::wire;

inline constexpr const char* kEdgeHost = "edge.test";
inline constexpr u64 kEdgeTimeMs = 1'700'000'000'000;
// Server-opened unidirectional stream ids, as QUIC numbers them.
inline constexpr u64 kSnapshotStream = 3;

[[nodiscard]] inline IpAddress ipv4(u8 a, u8 b, u8 c, u8 d) {
    return IpAddress::v4((u32{a} << 24) | (u32{b} << 16) | (u32{c} << 8) | u32{d});
}

[[nodiscard]] inline ServerId server_id(u8 seed) {
    ServerId id;
    id.value.bytes.fill(seed);
    id.value.bytes[6] = 0x40;
    return id;
}

[[nodiscard]] inline wire::ListEntry entry(u64 handle, u8 seed, std::string name, std::string version = "8.51",
                                           u32 players = 0, u32 flags = wire::entry_flag::online | wire::entry_flag::reachable) {
    wire::ListEntry out;
    out.handle = handle;
    out.id = server_id(seed).value;
    out.name = std::move(name);
    out.author = "author";
    out.version = version;
    if (const auto parsed = GameVersion::parse(version)) out.bucket = parsed->bucket();
    else out.bucket = wire::kBucketOther;
    out.players = players;
    out.max_players = 100;
    out.flags = flags;
    out.created_ms = kEdgeTimeMs - (handle * 1000);
    return out;
}

class FakeOwnServers final : public IOwnServers {
public:
    [[nodiscard]] bool owns(const ServerId& id) const override {
        for (const ServerId& own : ids)
            if (own == id) return true;
        return false;
    }
    std::vector<ServerId> ids;
};

// The edge's side of one connection: decodes what the client sent and answers on the control stream.
class Edge {
public:
    explicit Edge(testing::FakeQuicPeer& peer) : peer_(&peer) {}

    [[nodiscard]] testing::FakeQuicPeer& peer() const { return *peer_; }
    [[nodiscard]] u64 control() const { return peer_->streams().front(); }

    // Frames the client sent on its control stream since the last call.
    std::vector<std::pair<wire::FrameType, std::vector<u8>>> frames() {
        std::vector<std::pair<wire::FrameType, std::vector<u8>>> out;
        if (peer_->streams().empty()) return out;
        const std::vector<u8> all = peer_->received(control());
        const std::span<const u8> fresh = std::span<const u8>(all).subspan(consumed_);
        REQUIRE(wire::for_each_frame(fresh, 1 << 20, [&](const wire::FrameView& frame) {
            out.emplace_back(frame.type, std::vector<u8>(frame.payload.begin(), frame.payload.end()));
            return true;
        }));
        consumed_ = all.size();
        return out;
    }

    // The next frame the client sent, which must be a T.
    template <class T>
    T expect() {
        for (auto& frame : frames()) queue_.push_back(std::move(frame));
        REQUIRE_FALSE(queue_.empty());
        auto [type, payload] = std::move(queue_.front());
        queue_.pop_front();
        REQUIRE(type == wire::frame_type_v<T>);
        T message;
        REQUIRE(wire::decode(payload, message));
        return message;
    }

    [[nodiscard]] bool idle() {
        for (auto& frame : frames()) queue_.push_back(std::move(frame));
        return queue_.empty();
    }

    template <class T>
    void send(const T& message) {
        peer_->send(control(), wire::frame_bytes(message), false);
    }

    void snapshot(const wire::Snapshot& snapshot, u64 stream = kSnapshotStream) {
        peer_->send(stream, wire::frame_bytes(snapshot), true);
    }

    void datagram(const wire::Delta& delta) { peer_->send_datagram(wire::frame_bytes(delta, wire::LenWidth::two)); }

    void welcome(u32 max_subscriptions = 8) {
        wire::Welcome welcome;
        welcome.edge_id = 42;
        welcome.features = wire::feature::datagrams;
        welcome.server_time_ms = kEdgeTimeMs;
        welcome.limits.max_subscriptions = max_subscriptions;
        welcome.limits.max_window = 200;
        welcome.limits.max_query_limit = 100;
        send(welcome);
    }

private:
    testing::FakeQuicPeer* peer_;
    std::size_t consumed_ = 0;
    std::deque<std::pair<wire::FrameType, std::vector<u8>>> queue_;
};

[[nodiscard]] inline BrowserSessionOptions session_options() {
    BrowserSessionOptions options;
    options.client_version = "reboot-test/1";
    options.connectivity_url = "https://connectivity.test/";
    return options;
}

struct SessionRig {
    explicit SessionRig(BrowserSessionOptions options = session_options())
        : session(BrowserSessionDeps{quic, resolver, http, rt.strand(), rt.timers(), rt.clock(), random, rt.events()},
                  RbsbEndpoint{kEdgeHost, Port{443}, std::nullopt, EndpointSource::Compiled}, std::move(options)) {
        dns.set(kEdgeHost, {ipv4(10, 0, 0, 1)});
    }

    // Runs the session from Idle to Connected through a fresh connection.
    Edge& connect(u32 max_subscriptions = 8) {
        rt.run_until_idle();
        testing::FakeQuicPeer* peer = quic.last();
        REQUIRE(peer != nullptr);
        peer->accept();
        rt.run_until_idle();
        edges.push_back(std::make_unique<Edge>(*peer));
        Edge& edge = *edges.back();
        const wire::Hello hello = edge.expect<wire::Hello>();
        CHECK(hello.role == wire::Role::browser);
        CHECK(hello.features == wire::feature::datagrams);
        edge.welcome(max_subscriptions);
        rt.run_until_idle();
        REQUIRE(session.status().state == ConnectionState::Connected);
        return edge;
    }

    testing::DeterministicRuntime rt;
    testing::FakeRandom random{7};
    testing::FakeQuicTransport quic{rt.strand()};
    testing::FakeResolver dns{rt.strand()};
    testing::FakeHttpTransport http_transport{rt.strand(), rt.clock()};
    net::HostTlsMemory tls{{}, [](std::vector<storage::UpstreamTlsMemory>) {}};
    net::HttpClient http{http_transport, tls, rt.strand(), rt.timers(), random};
    net::AddressResolver resolver{dns, rt.strand(), rt.timers()};
    BrowserSession session;
    std::vector<std::unique_ptr<Edge>> edges;
};

// Captures one `done` and checks it ran exactly once.
template <class T>
struct Capture {
    [[nodiscard]] UniqueFunction<void(T)> callback() {
        return [this](T value) {
            ++calls;
            result.emplace(std::move(value));
        };
    }
    int calls = 0;
    std::optional<T> result;
};

}  // namespace rb::browser::test
