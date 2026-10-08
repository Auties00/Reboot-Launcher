#include <cstddef>
#include <cstdint>
#include <span>

#include "frame_fuzz.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/framing.hpp"

namespace common = reboot::contracts::common;
namespace gc = reboot::contracts::game_client;
namespace wh = reboot::contracts::winhost;

// A game-control connection: the preamble, then common, client-DLL and winhost frames.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const reboot::u8> input(data, size);
    if (input.size() < reboot::kGameControlPreambleSize) return 0;
    const auto preamble = input.first<reboot::kGameControlPreambleSize>();
    if (!reboot::parse_game_control_preamble(preamble)) return 0;
    reboot::testing::fuzz_frames<common::Ping, common::Pong, common::CommandResult, common::Unsupported, common::Log,
                                 gc::GcHello, gc::GcWelcome, gc::Loaded, gc::PatchResult, gc::RedirectReady,
                                 gc::HookFailed, gc::LoggedIn, gc::WindowCreated, gc::ExitRequested, gc::ConsoleReady,
                                 gc::Fatal, gc::GcShutdown, gc::TestJoin, gc::TestQuit, wh::WhHello, wh::WhWelcome,
                                 wh::Spawned, wh::Injected, wh::Output, wh::Exited, wh::WhFatal, wh::Resume, wh::Inject,
                                 wh::Stop>(input.subspan(reboot::kGameControlPreambleSize),
                                           reboot::kGameControlFrameCap);
    return 0;
}
