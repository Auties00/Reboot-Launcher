#pragma once

#include <memory>
#include <span>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/game_lifecycle_event.hpp"
#include "reboot/game_channel/lifecycle_markers.hpp"

namespace reboot::game_channel {

enum class OutputSource : u8 { Stdout, Stderr, UeLog };

// Covers game-launch.output-monitoring.
// Strand-only, one per session without our client DLL (a custom auth DLL). Logs stdout and stderr
// lines as GameOutput and turns marker lines into the events our DLL would send. stdout and the
// UE log repeat the same lines, so each event is emitted once, and nothing follows a SessionFatal
// or an ExitRequested.
class LegacyOutputAdapter {
public:
    LegacyOutputAdapter(SessionId session, const LifecycleMarkers& markers,
                        UniqueFunction<void(GameLifecycleEvent)> on_event);
    ~LegacyOutputAdapter();
    LegacyOutputAdapter(const LegacyOutputAdapter&) = delete;
    LegacyOutputAdapter& operator=(const LegacyOutputAdapter&) = delete;

    // Raw chunks: ISessionHost Output for stdout and stderr, UeLogTail for the UE log.
    void feed(OutputSource source, std::span<const u8> bytes);
    // End of `source`: its unterminated last line is matched too.
    void finish(OutputSource source);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::game_channel
