#pragma once

#include <chrono>
#include <string>

#include "reboot/contracts/ipc.hpp"
#include "reboot/engine/engine_origin.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ipc/ipc_server.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::engine {

struct SelfProcess {
    u32 pid = 0;
    std::chrono::system_clock::time_point created;
    NativePath image_path;
};

// What HelloAck, Engine.status, Engine.info and runtime.json say about this engine.
struct EngineInfo {
    std::string build{VersionStreams::ipc_build};
    SemVer app_version;
    // Random per start, so a reconnecting client can tell its ops and sessions are gone.
    EngineEpoch epoch;
    SelfProcess self;
    NativePath canonical_root;
    std::string root_hash16;
    // The pipe name or socket path.
    std::string endpoint;
    EngineOrigin origin{};
    contracts::ipc::StorageMode storage_mode{};
    // Follows the secret store, e.g. once the macOS Keychain unlocks.
    bool secrets_available = false;
    ports::OsInfo os;
};

[[nodiscard]] ipc::EngineHello to_engine_hello(const EngineInfo& info);

}  // namespace rb::engine
