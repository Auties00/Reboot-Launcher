#pragma once

// macOS <unistd.h> declares ::reboot(int), which clashes with namespace rb, so it is renamed
// while the header is read. Include this instead of <unistd.h>.
#if defined(__APPLE__)
#define reboot darwin_reboot
#include <unistd.h>
#undef reboot
#else
#include <unistd.h>
#endif
