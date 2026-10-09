#include "channel_core.hpp"

#include <algorithm>
#include <utility>

#include "reboot/foundation/log.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::game_channel {

namespace {

namespace gc = contracts::game_client;
namespace wh = contracts::winhost;

[[nodiscard]] std::span<const u8, kControlTokenSize> token_of(const PeerHello& hello) noexcept {
    return std::visit([](const auto& message) { return std::span<const u8, kControlTokenSize>(message.token); }, hello);
}

[[nodiscard]] gc::PeerRole role_of(const PeerHello& hello) noexcept {
    return std::holds_alternative<wh::WhHello>(hello) ? gc::PeerRole::Winhost : gc::PeerRole::ClientDll;
}

// The decoded Hello holds the token; it is wiped once the claim is made.
struct WipedHello {
    PeerHello hello;

    ~WipedHello() {
        std::visit([](auto& message) { secure_wipe(message.token.data(), message.token.size()); }, hello);
    }
};

}  // namespace

Diagnostic malformed_frame(u64 frame_type) {
    return make_diag(ErrorDomain::Contracts, kMalformedFrame).arg("frame_type", frame_type).build();
}

Inbound InboundDecoder::feed(std::span<const u8> bytes) {
    Inbound inbound;
    if (stopped_) return inbound;
    if (have_ < preamble_.size()) {
        const std::size_t take = std::min(bytes.size(), preamble_.size() - have_);
        std::copy_n(bytes.begin(), take, preamble_.begin() + static_cast<std::ptrdiff_t>(have_));
        have_ += take;
        bytes = bytes.subspan(take);
        if (have_ < preamble_.size()) return inbound;
        inbound.preamble = true;
        inbound.payload_abi = parse_game_control_preamble(preamble_);
        if (!inbound.payload_abi) {
            stopped_ = true;
            return inbound;
        }
    }
    const Framer::Status status = framer_.feed(bytes, [&](const RawFrame& frame) {
        inbound.frames.push_back(
            InboundFrame{frame.type, SecretBytes(std::vector<u8>(frame.payload.begin(), frame.payload.end()))});
        return true;
    });
    if (status != Framer::Status::ok) {
        stopped_ = true;
        inbound.failure = make_diag(ErrorDomain::Contracts, kMalformedFrame)
                              .arg("frame_type", u64{0})
                              .detail(status == Framer::Status::too_large ? "frame over the size cap" : "bad frame header")
                              .build();
    }
    return inbound;
}

void ChannelCore::adopt(std::unique_ptr<ports::IByteStream> stream) {
    if (closed_) {
        stream->close();
        return;
    }
    const u64 id = next_id_++;
    Connection& connection = connections_[id];
    connection.stream = std::move(stream);
    const std::weak_ptr<ChannelCore> weak = weak_from_this();
    Executor& strand = strand_;

    // I/O thread: decode here, act on the strand.
    connection.stream->on_read([weak, &strand, id, decoder = std::make_unique<InboundDecoder>()](std::span<const u8> bytes) {
        Inbound inbound = decoder->feed(bytes);
        if (inbound.empty()) return;
        strand.post([weak, id, inbound = std::move(inbound)]() mutable {
            if (const auto self = weak.lock()) self->on_inbound(id, std::move(inbound));
        });
    });
    connection.stream->on_close([weak, &strand, id] {
        strand.post([weak, id] {
            if (const auto self = weak.lock()) self->on_stream_closed(id);
        });
    });

    const std::chrono::milliseconds deadline = tokens_.hello_deadline().value_or(kHelloBase);
    connection.hello_timer = timers_.after(deadline, [this, id, deadline] {
        const auto it = connections_.find(id);
        if (it == connections_.end() || it->second.hello_seen) return;
        reject(id, to_diagnostic(GameChannelError{.code = GameChannelErrorCode::HelloTimeout, .timeout = deadline}));
    });
}

void ChannelCore::close() {
    closed_ = true;
    std::vector<PeerKey> bound;
    for (const auto& [id, connection] : connections_)
        if (connection.peer != nullptr) bound.push_back(connection.peer->key());
    // Emptied before any peer hears of it, since a peer's handler may drop it.
    connections_.clear();
    for (const PeerKey& key : bound)
        if (PeerLink* link = peer_of(key)) link->on_connection_closed();
}

void ChannelCore::attach(PeerLink& link) { peers_[link.key()] = &link; }

void ChannelCore::detach(const PeerKey& key) noexcept {
    const auto it = peers_.find(key);
    if (it != peers_.end()) peers_.erase(it);
}

void ChannelCore::write(u64 connection, std::span<const u8> bytes) {
    const auto it = connections_.find(connection);
    if (it != connections_.end()) it->second.stream->write(bytes);
}

void ChannelCore::close_connection(u64 connection) noexcept {
    connections_.erase(connection);
}

void ChannelCore::on_inbound(u64 id, Inbound inbound) {
    auto it = connections_.find(id);
    if (it == connections_.end()) return;
    if (inbound.preamble) {
        if (!inbound.payload_abi) {
            reject(id, to_diagnostic(GameChannelError{.code = GameChannelErrorCode::BadPreamble}));
            return;
        }
        it->second.payload_abi = inbound.payload_abi;
    }
    for (const InboundFrame& frame : inbound.frames) {
        it = connections_.find(id);
        if (it == connections_.end()) return;
        if (it->second.peer != nullptr) {
            it->second.peer->on_frame(frame.raw());
        } else if (!it->second.hello_seen) {
            on_hello(id, frame);
        }
    }
    if (!inbound.failure) return;
    it = connections_.find(id);
    if (it == connections_.end()) return;
    if (it->second.peer != nullptr && it->second.peer->welcomed()) {
        it->second.peer->on_protocol_error(std::move(*inbound.failure));
        return;
    }
    reject(id, *inbound.failure);
}

void ChannelCore::on_stream_closed(u64 id) {
    const auto it = connections_.find(id);
    if (it == connections_.end()) return;
    std::optional<PeerKey> bound;
    if (it->second.peer != nullptr) bound = it->second.peer->key();
    close_connection(id);
    if (!bound) {
        REBOOT_LOG_DEBUG(Play, "game channel connection {} closed before it was claimed", id);
        return;
    }
    if (PeerLink* link = peer_of(*bound)) link->on_connection_closed();
}

void ChannelCore::on_hello(u64 id, const InboundFrame& frame) {
    Connection& connection = connections_.at(id);
    connection.hello_seen = true;
    connection.hello_timer.cancel();

    WipedHello decoded;
    const RawFrame raw = frame.raw();
    // Frames follow only a valid preamble, so payload_abi is set.
    const u16 payload_abi = *connection.payload_abi;
    if (is_frame<gc::GcHello>(raw)) {
        auto hello = decode_contract<gc::GcHello>(raw);
        // A GcHello always speaks for our client DLL; winhost has its own Hello.
        if (!hello || hello->role != gc::PeerRole::ClientDll) {
            if (hello) secure_wipe(hello->token.data(), hello->token.size());
            reject(id, malformed_frame(raw.type));
            return;
        }
        decoded.hello = std::move(*hello);
        // Moving a std::array copies it.
        secure_wipe(hello->token.data(), hello->token.size());
    } else if (is_frame<wh::WhHello>(raw)) {
        auto hello = decode_contract<wh::WhHello>(raw);
        if (!hello) {
            reject(id, malformed_frame(raw.type));
            return;
        }
        decoded.hello = std::move(*hello);
        secure_wipe(hello->token.data(), hello->token.size());
    } else {
        reject(id, make_diag(ErrorDomain::Contracts, kUnexpectedFrame)
                       .arg("expected", contract_frame_type_v<gc::GcHello>)
                       .arg("actual", raw.type)
                       .build());
        return;
    }

    const auto claimed = tokens_.claim(token_of(decoded.hello), role_of(decoded.hello));
    if (!claimed) {
        reject(id, claimed.error());
        return;
    }
    // A token issued outside this listener has no peer to serve it.
    PeerLink* link = peer_of(*claimed);
    if (link == nullptr) {
        reject(id, to_diagnostic(GameChannelError{.code = GameChannelErrorCode::UnknownToken}));
        return;
    }
    connection.peer = link;
    link->accept(id, decoded.hello, payload_abi);
}

void ChannelCore::reject(u64 id, const Diagnostic& why) {
    REBOOT_LOG_WARN(Play, "game channel connection {} refused: {}", id, why.id);
    close_connection(id);
}

PeerLink* ChannelCore::peer_of(const PeerKey& key) const noexcept {
    const auto it = peers_.find(key);
    return it == peers_.end() ? nullptr : it->second;
}

}  // namespace reboot::game_channel
