#include <cstddef>
#include <cstdint>
#include <span>

#include "frame_fuzz.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/framing.hpp"

namespace common = rb::contracts::common;
namespace gc = rb::contracts::game_client;
namespace wh = rb::contracts::winhost;

// A game-control connection: the preamble, then common, client-DLL and winhost frames.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const rb::u8> input(data, size);
    if (input.size() < rb::kGameControlPreambleSize) return 0;
    const auto preamble = input.first<rb::kGameControlPreambleSize>();
    if (!rb::parse_game_control_preamble(preamble)) return 0;
    rb::testing::fuzz_frames<common::Ping, common::Pong, common::CommandResult, common::Unsupported, common::Log,
                                 gc::GcHello, gc::GcWelcome, gc::Loaded, gc::PatchResult, gc::RedirectReady,
                                 gc::HookFailed, gc::LoggedIn, gc::WindowCreated, gc::ExitRequested, gc::ConsoleReady,
                                 gc::Fatal, gc::GcShutdown, gc::TestJoin, gc::TestQuit, wh::WhHello, wh::WhWelcome,
                                 wh::Spawned, wh::Injected, wh::Output, wh::Exited, wh::WhFatal, wh::Resume, wh::Inject,
                                 wh::Stop>(input.subspan(rb::kGameControlPreambleSize),
                                           rb::kGameControlFrameCap);
    return 0;
}
