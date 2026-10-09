#include <format>
#include <utility>

#include "channel_core.hpp"
#include "peer_role.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/game_channel/token_registry.hpp"

namespace reboot::game_channel {

namespace {

namespace common = contracts::common;

}  // namespace

PeerLink::PeerLink(ChannelCore& core, PeerKey key, ControlToken token, UniqueFunction<void(PeerLiveness)> on_liveness,
                   UniqueFunction<void(Diagnostic)> on_lost)
    : core_(core),
      key_(std::move(key)),
      token_(std::move(token)),
      on_liveness_(std::move(on_liveness)),
      on_lost_(std::move(on_lost)) {
    core_.attach(*this);
}

PeerLink::~PeerLink() {
    core_.detach(key_);
    core_.tokens().revoke(key_);
    if (connection_) core_.close_connection(*connection_);
}

GameChannelError PeerLink::error(GameChannelErrorCode code, std::string request) const {
    return GameChannelError{.code = code, .role = key_.role, .module = key_.module, .request = std::move(request)};
}

void PeerLink::accept(u64 connection, const PeerHello& hello, u16 payload_abi) {
    connection_ = connection;
    if (payload_abi != contracts::game_client::kPayloadAbi) {
        GameChannelError mismatch = error(GameChannelErrorCode::PayloadAbiMismatch);
        mismatch.expected_version = contracts::game_client::kPayloadAbi;
        mismatch.actual_version = payload_abi;
        refuse(to_diagnostic(mismatch));
        return;
    }
    const u32 protocol = std::visit([](const auto& message) { return message.protocol; }, hello);
    if (protocol != expected_protocol(key_.role)) {
        GameChannelError mismatch = error(GameChannelErrorCode::ProtocolMismatch);
        mismatch.expected_version = expected_protocol(key_.role);
        mismatch.actual_version = protocol;
        refuse(to_diagnostic(mismatch));
        return;
    }

    const std::weak_ptr<void> alive = guard();
    Result<SecretBytes> frame = welcome(hello);
    if (alive.expired()) return;
    if (!frame) {
        refuse(std::move(frame.error()));
        return;
    }
    if (!connection_) return;
    core_.write(*connection_, frame->reveal());
    welcomed_ = true;
    REBOOT_LOG_AT(LogLevel::Info, Play, key_.session, "game channel welcomed the {} {}", role_name(key_.role), key_.module);
    arm_ping();
}

void PeerLink::on_frame(const RawFrame& frame) {
    if (lost_) return;
    if (is_frame<common::Ping>(frame)) {
        const auto ping = decode_contract<common::Ping>(frame);
        if (!ping) return on_protocol_error(ping.error());
        if (connection_) core_.write(*connection_, encode_contract_frame(common::Pong{ping->nonce}));
    } else if (is_frame<common::Pong>(frame)) {
        const auto pong = decode_contract<common::Pong>(frame);
        if (!pong) return on_protocol_error(pong.error());
        on_pong(pong->nonce);
    } else if (is_frame<common::CommandResult>(frame)) {
        auto result = decode_contract<common::CommandResult>(frame);
        if (!result) return on_protocol_error(result.error());
        if (result->ok) return reply(result->req_id, {});
        const auto it = pending_.find(result->req_id);
        GameChannelError failed = error(GameChannelErrorCode::RequestFailed, it == pending_.end() ? "" : it->second.request);
        if (result->error) failed.cause = common::to_diagnostic(*result->error);
        reply(result->req_id, std::unexpected(to_diagnostic(failed)));
    } else if (is_frame<common::Unsupported>(frame)) {
        const auto unsupported = decode_contract<common::Unsupported>(frame);
        if (!unsupported) return on_protocol_error(unsupported.error());
        const auto it = pending_.find(unsupported->req_id);
        const std::string request = it == pending_.end() ? "" : it->second.request;
        reply(unsupported->req_id, std::unexpected(to_diagnostic(error(GameChannelErrorCode::UnsupportedRequest, request))));
    } else if (is_frame<common::Log>(frame)) {
        const auto log = decode_contract<common::Log>(frame);
        if (!log) return on_protocol_error(log.error());
        log_peer(*log);
    } else {
        const std::weak_ptr<void> alive = guard();
        Result<void> handled = on_role_frame(frame);
        if (alive.expired() || handled) return;
        on_protocol_error(std::move(handled.error()));
    }
}

void PeerLink::on_connection_closed() {
    connection_.reset();
    if (welcomed_) lose(to_diagnostic(error(GameChannelErrorCode::PeerLost)));
}

void PeerLink::on_protocol_error(Diagnostic cause) {
    REBOOT_LOG_AT(LogLevel::Warn, Play, key_.session, "the {} {} broke the game channel protocol: {}", role_name(key_.role),
                  key_.module, cause.id);
    if (connection_) core_.close_connection(*std::exchange(connection_, std::nullopt));
    GameChannelError lost = error(GameChannelErrorCode::PeerLost);
    lost.cause = std::move(cause);
    lose(to_diagnostic(lost));
}

Result<void> PeerLink::request(std::string_view name, UniqueFunction<std::vector<u8>(u64 req_id)> encode,
                               ReplyHandler done) {
    if (!welcomed_) return std::unexpected(to_diagnostic(error(GameChannelErrorCode::NotWelcomed)));
    if (lost_ || !connection_) {
        core_.strand().post([alive = guard(), done = std::move(done),
                             lost = to_diagnostic(error(GameChannelErrorCode::PeerLost))]() mutable {
            if (!alive.expired()) done(std::unexpected(std::move(lost)));
        });
        return {};
    }
    const u64 req_id = next_req_id_++;
    pending_.emplace(req_id, Pending{std::string(name), std::move(done)});
    const SecretBytes frame(encode(req_id));
    core_.write(*connection_, frame.reveal());
    return {};
}

void PeerLink::lose(Diagnostic diag) {
    if (lost_) return;
    lost_ = true;
    ping_timer_.cancel();
    REBOOT_LOG_AT(LogLevel::Warn, Play, key_.session, "game channel lost the {} {}: {}", role_name(key_.role), key_.module,
                  diag.id);
    const std::weak_ptr<void> alive = guard();
    std::map<u64, Pending> abandoned = std::exchange(pending_, {});
    for (auto& [req_id, pending] : abandoned) {
        pending.done(std::unexpected(to_diagnostic(error(GameChannelErrorCode::PeerLost, pending.request))));
        if (alive.expired()) return;
    }
    UniqueFunction<void(Diagnostic)> handler = std::move(on_lost_);
    if (handler) handler(std::move(diag));
}

void PeerLink::refuse(Diagnostic diag) {
    if (connection_) core_.close_connection(*std::exchange(connection_, std::nullopt));
    lose(std::move(diag));
}

void PeerLink::reply(u64 req_id, Result<void> outcome) {
    const auto it = pending_.find(req_id);
    if (it == pending_.end()) {
        REBOOT_LOG_AT(LogLevel::Debug, Play, key_.session, "the {} answered unknown request {}", role_name(key_.role), req_id);
        return;
    }
    ReplyHandler done = std::move(it->second.done);
    pending_.erase(it);
    done(std::move(outcome));
}

void PeerLink::arm_ping() {
    ping_timer_ = core_.timers().after(liveness_.interval, [this] { ping_due(); });
}

void PeerLink::ping_due() {
    if (lost_ || !connection_) return;
    if (!pong_since_ping_) {
        ++missed_;
        if (missed_ >= liveness_.miss_limit && responsive_) {
            responsive_ = false;
            if (!notify(on_liveness_, PeerLiveness::Unresponsive)) return;
            if (lost_ || !connection_) return;
        }
    }
    pong_since_ping_ = false;
    core_.write(*connection_, encode_contract_frame(common::Ping{++ping_nonce_}));
    arm_ping();
}

// A Pong counts only when its nonce advances: our DLL answers with its game-thread tick, which
// stands still while the game thread hangs.
void PeerLink::on_pong(u64 nonce) {
    if (last_pong_ && nonce <= *last_pong_) return;
    last_pong_ = nonce;
    pong_since_ping_ = true;
    missed_ = 0;
    if (responsive_) return;
    responsive_ = true;
    notify(on_liveness_, PeerLiveness::Responsive);
}

void PeerLink::log_peer(const common::Log& log) const {
    const LogLevel level = log.level > LogLevel::Error ? LogLevel::Error : log.level;
    if (!Logger::enabled(level)) return;
    Logger::write(level, log_category(), key_.session,
                  std::format("{}: {}", key_.module, sanitize_display_text(log.text)));
}

}  // namespace reboot::game_channel
