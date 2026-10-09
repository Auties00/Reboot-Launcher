#pragma once

#include <string>
#include <vector>

#include "reboot/compat/runtime_id.hpp"
#include "reboot/compat/vc_redist_source.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::compat {

// What PrefixManager::prepare brings a prefix to.
struct PrefixRequest {
    ports::RuntimeLayout layout;
    RuntimeId runtime;
    std::string runtime_version;
    // The user's environment as the platform reports it, EnvBuilder's daemon base for the prefix
    // commands; PrefixManager adds the runner layer.
    ports::EnvBlock env;
    // Host paths of every DLL loaded into the game: the injected boot DLL and each DLL it loads
    // with LoadLibraryW, the custom auth DLL included. Their imports decide VC++ seeding.
    std::vector<NativePath> game_dlls;
    VcRedistSource vc_redist;
};

}  // namespace reboot::compat
