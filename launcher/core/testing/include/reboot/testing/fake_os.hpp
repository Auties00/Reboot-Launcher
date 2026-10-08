#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::testing {

// Which OS's shape the fakes take where the ports differ by OS.
enum class FakeOs : u8 {
    // session_host set, runner null, peer inspector unsupported, Job-kill exits.
    Windows,
    // runner set with MacRuntime, session_host null, peer inspector unsupported.
    MacOs,
    // runner set with Umu and Wine, session_host null, peer inspector supported.
    Linux,
};

}  // namespace reboot::testing
