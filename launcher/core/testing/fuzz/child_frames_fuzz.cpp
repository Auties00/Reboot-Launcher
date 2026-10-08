#include <cstddef>
#include <cstdint>
#include <span>

#include "frame_fuzz.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/framing.hpp"

namespace backend = reboot::contracts::backend;
namespace common = reboot::contracts::common;
namespace gs = reboot::contracts::game_server;

// Child stdio carries the common, backend and game-server messages.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    reboot::testing::fuzz_frames<
        common::Ping, common::Pong, common::CommandResult, common::Unsupported, common::Log, backend::BackendHello,
        backend::BackendWelcome, backend::Ready, backend::RegisterAccount, backend::RenameAccount,
        backend::MintLaunchCredential, backend::LaunchCredential, backend::ConfigureSession, backend::EndSession,
        backend::AccountsList, backend::AccountsReset, backend::AccountsDelete, backend::AccountsPrune,
        backend::AccountsReply, backend::PurgeData, backend::ContentInfo, backend::ContentInfoReply, backend::Health,
        backend::HealthReply, backend::Drain, backend::ResolveMatchTarget, backend::MatchTarget, backend::LoginObserved,
        backend::AccountRenameConflict,
        gs::GameServerDescription, gs::ServerHello, gs::ServerWelcome, gs::Listening, gs::ListenFailed,
        gs::StateChanged, gs::PlayerJoined, gs::PlayerLeft, gs::PlayerCount, gs::MatchEnded, gs::Fatal,
        gs::StartMatch, gs::EndMatch, gs::Reset, gs::Kick, gs::SetBans, gs::SetOperators, gs::RunCommand, gs::Drain,
        gs::Shutdown>(std::span<const reboot::u8>(data, size), reboot::kChildFrameCap);
    return 0;
}
