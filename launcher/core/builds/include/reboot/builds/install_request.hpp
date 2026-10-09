#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/support/support_role.hpp"

namespace rb::builds {

struct InstallRequest {
    // A catalog id or any of its aliases.
    CatalogEntryId entry;
    // Missing or an empty folder; a download staged for it earlier resumes.
    NativePath destination;
    std::string name;
    // Roles the new build is selected for once it is registered.
    std::vector<support::SupportRole> select_for;
};

}  // namespace rb::builds
