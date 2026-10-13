#pragma once

#include <optional>

#include "reboot/base/diag.hpp"
#include "reboot/base/native_path.hpp"
#include "reboot/base/types.hpp"

namespace rb::publish {

// A file that could not be read at load; the profile gets a fresh identity on its next use.
struct IdentityLoadIssue {
    HostProfileId profile;
    Diagnostic reason;
    std::optional<NativePath> quarantined_to;
};

}  // namespace rb::publish
