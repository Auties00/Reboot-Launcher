#pragma once

// Included first by every source of this package, before any system or reboot header.
// <unistd.h> declares ::reboot(int), which clashes with namespace reboot, so it is renamed while
// the header is read; later includes of <unistd.h> are then no-ops.
#define reboot darwin_reboot
#include <unistd.h>
#undef reboot

// CoreServices pulls in AssertMacros.h, whose unprefixed check/require/verify macros break C++.
#ifndef __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#endif
