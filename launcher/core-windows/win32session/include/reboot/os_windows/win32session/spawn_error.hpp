#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::os_windows::win32session {

// Where launching the session tree failed. A failure at any step rolls the whole launch back by
// terminating the Job, so no suspended companion is ever orphaned.
enum class SpawnStep : u8 {
    CreateJob,
    ConfigureJob,
    CreatePipe,
    AttributeList,
    CreateGame,
    CreateCompanion,
    Inject,
    Resume,
    // Starting a pipe-reader or exit-wait thread.
    Watch,
};

struct SpawnError {
    SpawnStep step{};
    SystemError error{};
};

}  // namespace reboot::os_windows::win32session
