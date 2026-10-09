#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::builds {

// Catalog: a download whose files did not settle the version. User: a ChooseVersion answer or a
// CLI --version.
enum class VersionSource : u8 { PeResource, ClTable, RawScan, Catalog, User };

}  // namespace rb::builds
