#pragma once

#include "reboot/compat/path_mapper.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::compat {

// A prefix brought to the session's runtime, with its drive mapping.
struct PreparedPrefix {
    NativePath dir;
    PathMapper paths;
};

}  // namespace rb::compat
