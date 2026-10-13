#pragma once

// <unistd.h> declares ::reboot(int), which clashes with namespace rb, so it is renamed while
// the header is read. Include this before any system header instead of <unistd.h>.
#define reboot darwin_reboot
#include <unistd.h>
#undef reboot
