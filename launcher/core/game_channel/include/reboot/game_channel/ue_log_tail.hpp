#pragma once

#include <chrono>
#include <memory>
#include <span>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class Executor;
class TimerService;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::game_channel {

// A file the game holds open does not reliably raise change notifications while it grows.
inline constexpr std::chrono::milliseconds kUeLogPollInterval{500};

// Covers game-launch.output-monitoring (the UE log input of LegacyOutputAdapter).
// Strand-only. Delivers only bytes written after start(), so a previous run's markers never count;
// a new file id, or a size below the read offset, restarts from offset 0. Reads go through
// IFileSystem::read_shared on the WorkerPool, which never blocks UE renaming its old log.
class UeLogTail {
public:
    UeLogTail(ports::IFileSystem& files, WorkerPool& workers, Executor& strand, TimerService& timers, NativePath path,
              UniqueFunction<void(std::span<const u8>)> on_bytes);
    // A read in flight is discarded.
    ~UeLogTail();
    UeLogTail(const UeLogTail&) = delete;
    UeLogTail& operator=(const UeLogTail&) = delete;

    // `ready` runs on the strand once the baseline is recorded; only then may the game launch.
    // A missing file is waited for; a read error is logged once (game_channel.log_unreadable).
    void start(UniqueFunction<void()> ready);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::game_channel
