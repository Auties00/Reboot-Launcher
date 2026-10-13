#pragma once

#include "reboot/base/native_path.hpp"
#include "reboot/compat/path_mapper.hpp"

namespace rb::compat {

// A prefix brought to the session's runtime, with its drive mapping.
struct PreparedPrefix {
    NativePath dir;
    PathMapper paths;
};

}  // namespace rb::compat
