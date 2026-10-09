#include "reboot/engine/engine_info.hpp"

namespace rb::engine {

ipc::EngineHello to_engine_hello(const EngineInfo& info) {
    return ipc::EngineHello{
        .engine_build = info.build,
        .epoch = info.epoch,
        .pid = info.self.pid,
        .image_path = info.self.image_path,
        .canonical_root = info.canonical_root,
        .origin = info.origin,
        .storage_mode = info.storage_mode,
        .secrets_available = info.secrets_available,
    };
}

}  // namespace rb::engine
