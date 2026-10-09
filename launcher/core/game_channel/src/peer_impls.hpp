#pragma once

#include "channel_core.hpp"
#include "reboot/game_channel/client_dll_peer.hpp"
#include "reboot/game_channel/winhost_peer.hpp"

namespace rb::game_channel {

struct ClientDllPeer::Impl final : PeerLink {
    Impl(ChannelCore& core, PeerKey key, ControlToken token, ClientDllHandlers handlers);

    Result<SecretBytes> welcome(const PeerHello& hello) override;
    Result<void> on_role_frame(const RawFrame& frame) override;
    [[nodiscard]] LogCategory log_category() const noexcept override { return LogCategory::Play; }

    Result<void> test_request(std::string_view name, UniqueFunction<std::vector<u8>(u64 req_id)> encode, ReplyHandler done);

    UniqueFunction<Result<contracts::game_client::ClientDllConfig>(const ClientDllHello&)> configure;
    UniqueFunction<void(GameLifecycleEvent)> on_event;
    bool test_mode = false;
};

struct WinhostPeer::Impl final : PeerLink {
    Impl(ChannelCore& core, PeerKey key, ControlToken token, WinhostHandlers handlers);

    Result<SecretBytes> welcome(const PeerHello& hello) override;
    Result<void> on_role_frame(const RawFrame& frame) override;
    [[nodiscard]] LogCategory log_category() const noexcept override { return LogCategory::Wine; }

    UniqueFunction<Result<contracts::winhost::SpawnGame>(const WinhostHello&)> configure;
    UniqueFunction<void(WinhostEvent)> on_event;
};

}  // namespace rb::game_channel
