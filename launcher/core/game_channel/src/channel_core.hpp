#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/control_token.hpp"
#include "reboot/game_channel/peer_key.hpp"
#include "reboot/game_channel/peer_liveness.hpp"
#include "reboot/game_channel/reply_handler.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/process/liveness_policy.hpp"

#include "game_channel_error.hpp"

namespace reboot::game_channel {

class TokenRegistry;

// The wait for a Hello when no token is waiting for one.
inline constexpr std::chrono::milliseconds kHelloBase{2000};

// A received frame; wiped once handled, since a Hello carries its token.
struct InboundFrame {
    u64 type = 0;
    SecretBytes payload;

    [[nodiscard]] RawFrame raw() const noexcept { return {type, payload.reveal()}; }
};

// What one read decoded on the I/O thread.
struct Inbound {
    // The preamble completed in this read; payload_abi is nullopt when its magic did not match.
    bool preamble = false;
    std::optional<u16> payload_abi;
    std::vector<InboundFrame> frames;
    // The stream broke the framing; nothing after it is decoded.
    std::optional<Diagnostic> failure;

    [[nodiscard]] bool empty() const noexcept { return !preamble && frames.empty() && !failure; }
};

// One connection's bytes, preamble first, then frames of at most kGameControlFrameCap. I/O thread only.
class InboundDecoder {
public:
    Inbound feed(std::span<const u8> bytes);

private:
    std::array<u8, kGameControlPreambleSize> preamble_{};
    std::size_t have_ = 0;
    bool stopped_ = false;
    Framer framer_{kGameControlFrameCap};
};

using PeerHello = std::variant<contracts::game_client::GcHello, contracts::winhost::WhHello>;

// contracts.malformed_frame for a frame of `frame_type`.
[[nodiscard]] Diagnostic malformed_frame(u64 frame_type);

class ChannelCore;

// The strand-side state of one opened peer, shared by ClientDllPeer and WinhostPeer: the Hello
// checks, request correlation, liveness and the single on_lost.
class PeerLink {
public:
    PeerLink(ChannelCore& core, PeerKey key, ControlToken token, UniqueFunction<void(PeerLiveness)> on_liveness,
             UniqueFunction<void(Diagnostic)> on_lost);
    // Revokes the token and closes the connection without calling on_lost.
    virtual ~PeerLink();
    PeerLink(const PeerLink&) = delete;
    PeerLink& operator=(const PeerLink&) = delete;

    [[nodiscard]] const PeerKey& key() const noexcept { return key_; }
    [[nodiscard]] const ControlToken& token() const noexcept { return token_; }
    [[nodiscard]] bool welcomed() const noexcept { return welcomed_; }

    // The connection `connection` claimed this peer's token with `hello`: refused or welcomed.
    void accept(u64 connection, const PeerHello& hello, u16 payload_abi);
    void on_frame(const RawFrame& frame);
    // The connection ended by itself.
    void on_connection_closed();
    // The connection broke the protocol after the Hello.
    void on_protocol_error(Diagnostic cause);

    // not_welcomed before Welcome; peer_lost reaches `done` on the strand once the connection is gone.
    Result<void> request(std::string_view name, UniqueFunction<std::vector<u8>(u64 req_id)> encode, ReplyHandler done);

protected:
    // The encoded Welcome for `hello`, or why the peer is refused; may destroy this link.
    virtual Result<SecretBytes> welcome(const PeerHello& hello) = 0;
    // Emits the role's event; an error is a frame this role never sends, or one that does not decode.
    virtual Result<void> on_role_frame(const RawFrame& frame) = 0;
    [[nodiscard]] virtual LogCategory log_category() const noexcept = 0;

    // Calls the handler in `slot`, keeping it unless the call destroyed this link; false when it did.
    template <class... Args, class... Passed>
    bool notify(UniqueFunction<void(Args...)>& slot, Passed&&... args) {
        if (!slot) return true;
        const std::weak_ptr<void> guard = alive_;
        UniqueFunction<void(Args...)> handler = std::move(slot);
        handler(std::forward<Passed>(args)...);
        if (guard.expired()) return false;
        if (!slot) slot = std::move(handler);
        return true;
    }

    [[nodiscard]] std::weak_ptr<void> guard() const noexcept { return alive_; }
    [[nodiscard]] GameChannelError error(GameChannelErrorCode code, std::string request = {}) const;

private:
    struct Pending {
        std::string request;
        ReplyHandler done;
    };

    // Refuses before Welcome, or ends after it: pending replies get peer_lost, then on_lost runs once.
    void lose(Diagnostic diag);
    void refuse(Diagnostic diag);
    void reply(u64 req_id, Result<void> outcome);
    void arm_ping();
    void ping_due();
    void on_pong(u64 nonce);
    void log_peer(const contracts::common::Log& log) const;

    ChannelCore& core_;
    PeerKey key_;
    ControlToken token_;
    UniqueFunction<void(PeerLiveness)> on_liveness_;
    UniqueFunction<void(Diagnostic)> on_lost_;
    process::LivenessPolicy liveness_;
    std::optional<u64> connection_;
    bool welcomed_ = false;
    bool lost_ = false;
    u64 next_req_id_ = 1;
    std::map<u64, Pending> pending_;
    TimerHandle ping_timer_;
    u64 ping_nonce_ = 0;
    std::optional<u64> last_pong_;
    bool pong_since_ping_ = true;
    u32 missed_ = 0;
    bool responsive_ = true;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

// Strand-only. Connections from accept or adopt, their Hello deadline and token claim, and the
// routing of frames to the PeerLink that claimed each one.
class ChannelCore : public std::enable_shared_from_this<ChannelCore> {
public:
    ChannelCore(Executor& strand, TimerService& timers, TokenRegistry& tokens) noexcept
        : strand_(strand), timers_(timers), tokens_(tokens) {}
    ChannelCore(const ChannelCore&) = delete;
    ChannelCore& operator=(const ChannelCore&) = delete;

    [[nodiscard]] Executor& strand() noexcept { return strand_; }
    [[nodiscard]] TimerService& timers() noexcept { return timers_; }
    [[nodiscard]] TokenRegistry& tokens() noexcept { return tokens_; }

    // After close() a stream is closed at once.
    void adopt(std::unique_ptr<ports::IByteStream> stream);
    // Closes every connection; a welcomed peer reports peer_lost.
    void close();
    // Serves streams again after close().
    void reopen() noexcept { closed_ = false; }

    void attach(PeerLink& link);
    void detach(const PeerKey& key) noexcept;
    void write(u64 connection, std::span<const u8> bytes);
    // Drops the connection without telling its peer.
    void close_connection(u64 connection) noexcept;

private:
    struct Connection {
        std::unique_ptr<ports::IByteStream> stream;
        std::optional<u16> payload_abi;
        bool hello_seen = false;
        TimerHandle hello_timer;
        PeerLink* peer = nullptr;
    };

    void on_inbound(u64 id, Inbound inbound);
    void on_stream_closed(u64 id);
    void on_hello(u64 id, const InboundFrame& frame);
    void reject(u64 id, const Diagnostic& why);
    [[nodiscard]] PeerLink* peer_of(const PeerKey& key) const noexcept;

    Executor& strand_;
    TimerService& timers_;
    TokenRegistry& tokens_;
    std::map<u64, Connection> connections_;
    std::map<PeerKey, PeerLink*> peers_;
    u64 next_id_ = 1;
    bool closed_ = false;
};

}  // namespace reboot::game_channel
