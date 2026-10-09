#include "reboot/game_channel/client_dll_peer.hpp"

#include <memory>
#include <utility>

#include "peer_impls.hpp"

namespace rb::game_channel {

namespace {

namespace gc = contracts::game_client;

// Decodes `frame` as T and hands on_event what `convert` makes of it.
template <class T, class Convert>
[[nodiscard]] Result<void> emit_as(const RawFrame& frame, Convert&& convert, bool& matched) {
    if (matched || !is_frame<T>(frame)) return {};
    matched = true;
    auto message = decode_contract<T>(frame);
    if (!message) return std::unexpected(std::move(message.error()));
    convert(std::move(*message));
    return {};
}

}  // namespace

ClientDllPeer::Impl::Impl(ChannelCore& core, PeerKey key, ControlToken token, ClientDllHandlers handlers)
    : PeerLink(core, std::move(key), std::move(token), std::move(handlers.on_liveness), std::move(handlers.on_lost)),
      configure(std::move(handlers.configure)),
      on_event(std::move(handlers.on_event)) {}

Result<SecretBytes> ClientDllPeer::Impl::welcome(const PeerHello& hello) {
    const auto& dll = std::get<gc::GcHello>(hello);
    const ClientDllHello info{dll.dll_build, dll.game, dll.exe_sha256, dll.pid};
    if (!configure) return std::unexpected(internal_bug("game_channel.client_dll_configure"));
    // Called once; held locally so a handler that drops this peer does not destroy itself.
    auto handler = std::move(configure);
    const std::weak_ptr<void> alive = guard();
    Result<gc::ClientDllConfig> config = handler(info);
    // The caller drops the result once this peer is gone.
    if (alive.expired()) return std::unexpected(Diagnostic{});
    if (!config) return std::unexpected(std::move(config.error()));
    const bool requested_test_mode = config->test_mode;
    gc::GcWelcome welcome{std::move(*config)};
    SecretBytes frame(encode_contract_frame(welcome));
    // The origin carries the session's front key.
    secure_wipe(welcome.config.origin.data(), welcome.config.origin.size());
    test_mode = requested_test_mode;
    return frame;
}

Result<void> ClientDllPeer::Impl::on_role_frame(const RawFrame& frame) {
    bool matched = false;
    const auto emit = [this](GameLifecycleEvent event) { notify(on_event, std::move(event)); };
    const auto forward = [&emit](auto message) { emit(GameLifecycleEvent{std::move(message)}); };
    Result<void> handled = emit_as<gc::Loaded>(frame, forward, matched);
    if (handled) handled = emit_as<gc::PatchResult>(frame, forward, matched);
    if (handled) handled = emit_as<gc::RedirectReady>(frame, forward, matched);
    if (handled) handled = emit_as<gc::HookFailed>(frame, forward, matched);
    if (handled) handled = emit_as<gc::LoggedIn>(frame, forward, matched);
    if (handled) handled = emit_as<gc::WindowCreated>(frame, forward, matched);
    if (handled) handled = emit_as<gc::ExitRequested>(frame, forward, matched);
    if (handled) handled = emit_as<gc::ConsoleReady>(frame, forward, matched);
    if (handled) handled = emit_as<gc::TravelStarted>(frame, forward, matched);
    if (handled) handled = emit_as<gc::TravelEnded>(frame, forward, matched);
    if (handled) handled = emit_as<gc::Joined>(frame, forward, matched);
    if (handled) handled = emit_as<gc::Disconnected>(frame, forward, matched);
    if (handled)
        handled = emit_as<gc::Fatal>(
            frame, [&emit](gc::Fatal fatal) { emit(SessionFatal{FatalCause::DllStep, std::move(fatal.step)}); }, matched);
    if (!handled) return handled;
    if (!matched) return std::unexpected(malformed_frame(frame.type));
    return {};
}

Result<void> ClientDllPeer::Impl::test_request(std::string_view name, UniqueFunction<std::vector<u8>(u64 req_id)> encode,
                                               ReplyHandler done) {
    if (welcomed() && !test_mode)
        return std::unexpected(to_diagnostic(error(GameChannelErrorCode::TestModeOff, std::string(name))));
    return request(name, std::move(encode), std::move(done));
}

ClientDllPeer::ClientDllPeer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ClientDllPeer::~ClientDllPeer() = default;

const ControlToken& ClientDllPeer::token() const noexcept { return impl_->token(); }

bool ClientDllPeer::welcomed() const noexcept { return impl_->welcomed(); }

Result<void> ClientDllPeer::shutdown(ReplyHandler done) {
    return impl_->request(
        "shutdown", [](u64 req_id) { return encode_contract_frame(gc::GcShutdown{req_id}); }, std::move(done));
}

Result<void> ClientDllPeer::test_join(std::string address, ReplyHandler done) {
    return impl_->test_request(
        "test_join",
        [address = std::move(address)](u64 req_id) { return encode_contract_frame(gc::TestJoin{req_id, address}); },
        std::move(done));
}

Result<void> ClientDllPeer::test_quit(ReplyHandler done) {
    return impl_->test_request(
        "test_quit", [](u64 req_id) { return encode_contract_frame(gc::TestQuit{req_id}); }, std::move(done));
}

}  // namespace rb::game_channel
