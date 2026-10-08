#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/gameserver/describe_cache_document.hpp"
#include "reboot/gameserver/game_server_description.hpp"
#include "reboot/process/built_env.hpp"
#include "reboot/process/child_record.hpp"
#include "reboot/storage/document_store.hpp"

namespace reboot {
class Executor;
class IClock;
class TimerService;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class IProcessLauncher;
}  // namespace reboot::ports

namespace reboot::gameserver {

// InstallLayout::game_server_exe, or `dev_override` (a phase-3 build tree) when set.
// Lexical only; fails with gameserver.path_not_absolute.
[[nodiscard]] Result<NativePath> locate_game_server(const InstallLayout& install,
                                                    const std::optional<NativePath>& dev_override);

// The EnvBuilder base layer for a game-server child; built anew for each spawn because BuiltEnv
// is move-only.
using BaseEnvSource = UniqueFunction<Result<process::BuiltEnv>()>;

// Capabilities: none; implements owner-2 and game-server-dll-design (described sockets and commands).
// Strand-only. Hashes the exe on the WorkerPool through IFileSystem (re-hashed only when its
// FileRevision changes), then looks the sha256 up in the describe cache. On a miss it runs
// `<exe> --describe` with captured stdio, cwd at the exe's folder, the BaseEnvSource environment
// and the ChildHello deadline, expects exactly one GameServerDescription frame, checks the
// protocol and the sockets, and caches the result stamped with IClock::system_now().
class GameServerBinary {
public:
    GameServerBinary(NativePath exe, ports::IFileSystem& fs, ports::IProcessLauncher& launcher, WorkerPool& workers,
                     Executor& strand, TimerService& timers, const IClock& clock,
                     storage::DocumentStore<DescribeCacheDocument>& cache, BaseEnvSource base_env,
                     process::ChildRecordCallback record,
                     std::chrono::milliseconds describe_deadline = default_deadline(OpKind::ChildHello));
    // Kills a running --describe; pending callbacks are dropped.
    ~GameServerBinary();
    GameServerBinary(const GameServerBinary&) = delete;
    GameServerBinary& operator=(const GameServerBinary&) = delete;

    [[nodiscard]] const NativePath& exe() const noexcept;

    // Single flight: calls made while one runs share its result. `done` runs later on the strand,
    // exactly once.
    void describe(UniqueFunction<void(Result<DescribedBinary>)> done);

    // The last successful describe(), for the SupportPolicy host cells; null before the first.
    [[nodiscard]] const DescribedBinary* current() const noexcept;

    // Drops a cache entry that a ServerHello contradicted, so the next describe() runs the binary.
    void forget(const Sha256Digest& sha256);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::gameserver
