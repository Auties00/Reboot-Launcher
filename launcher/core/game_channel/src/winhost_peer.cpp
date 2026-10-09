#include "reboot/game_channel/winhost_peer.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include "peer_impls.hpp"

namespace reboot::game_channel {

namespace {

namespace wh = contracts::winhost;

void wipe(wh::Bytes& bytes) noexcept { secure_wipe(bytes.data(), bytes.size()); }

// The argv may hold -AUTH_PASSWORD and the environment the game's REBOOT_CTL_TOKEN.
void wipe_secrets(wh::SpawnGame& spawn) noexcept {
    for (wh::Bytes& arg : spawn.argv_utf16) wipe(arg);
    wipe(spawn.env_block_utf16);
    for (wh::CompanionSpawn& companion : spawn.companions)
        for (wh::Bytes& arg : companion.argv_utf16) wipe(arg);
}

template <class T>
[[nodiscard]] Result<void> emit_as(const RawFrame& frame, UniqueFunction<void(WinhostEvent)>& emit, bool& matched) {
    if (matched || !is_frame<T>(frame)) return {};
    matched = true;
    auto message = decode_contract<T>(frame);
    if (!message) return std::unexpected(std::move(message.error()));
    emit(WinhostEvent{std::move(*message)});
    return {};
}

}  // namespace

WinhostPeer::Impl::Impl(ChannelCore& core, PeerKey key, ControlToken token, WinhostHandlers handlers)
    : PeerLink(core, std::move(key), std::move(token), std::move(handlers.on_liveness), std::move(handlers.on_lost)),
      configure(std::move(handlers.configure)),
      on_event(std::move(handlers.on_event)) {}

Result<SecretBytes> WinhostPeer::Impl::welcome(const PeerHello& hello) {
    const auto& host = std::get<wh::WhHello>(hello);
    const WinhostHello info{host.build, host.pid};
    if (!configure) return std::unexpected(internal_bug("game_channel.winhost_configure"));
    auto handler = std::move(configure);
    const std::weak_ptr<void> alive = guard();
    Result<wh::SpawnGame> spawn = handler(info);
    // The caller drops the result once this peer is gone.
    if (alive.expired()) {
        if (spawn) wipe_secrets(*spawn);
        return std::unexpected(Diagnostic{});
    }
    if (!spawn) return std::unexpected(std::move(spawn.error()));
    wh::WhWelcome welcome{std::move(*spawn)};
    SecretBytes frame(encode_contract_frame(welcome));
    wipe_secrets(welcome.spawn);
    return frame;
}

Result<void> WinhostPeer::Impl::on_role_frame(const RawFrame& frame) {
    bool matched = false;
    UniqueFunction<void(WinhostEvent)> emit = [this](WinhostEvent event) { notify(on_event, std::move(event)); };
    Result<void> handled = emit_as<wh::Spawned>(frame, emit, matched);
    if (handled) handled = emit_as<wh::Injected>(frame, emit, matched);
    if (handled) handled = emit_as<wh::Output>(frame, emit, matched);
    if (handled) handled = emit_as<wh::Exited>(frame, emit, matched);
    if (handled) handled = emit_as<wh::WhFatal>(frame, emit, matched);
    if (!handled) return handled;
    if (!matched) return std::unexpected(malformed_frame(frame.type));
    return {};
}

WinhostPeer::WinhostPeer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

WinhostPeer::~WinhostPeer() = default;

const ControlToken& WinhostPeer::token() const noexcept { return impl_->token(); }

bool WinhostPeer::welcomed() const noexcept { return impl_->welcomed(); }

Result<void> WinhostPeer::resume(ReplyHandler done) {
    return impl_->request(
        "resume", [](u64 req_id) { return encode_contract_frame(wh::Resume{req_id}); }, std::move(done));
}

Result<void> WinhostPeer::inject(wh::InjectSpec entry, ReplyHandler done) {
    return impl_->request(
        "inject",
        [entry = std::move(entry)](u64 req_id) { return encode_contract_frame(wh::Inject{req_id, entry}); },
        std::move(done));
}

Result<void> WinhostPeer::stop(std::chrono::milliseconds grace, ReplyHandler done) {
    const auto grace_ms = static_cast<u32>(
        std::clamp<std::chrono::milliseconds::rep>(grace.count(), 0, std::numeric_limits<u32>::max()));
    return impl_->request(
        "stop", [grace_ms](u64 req_id) { return encode_contract_frame(wh::Stop{req_id, grace_ms}); }, std::move(done));
}

}  // namespace reboot::game_channel
