#include "reboot/foundation/random.hpp"

#include <algorithm>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <unistd.h>
#endif

namespace reboot {

void OsRandom::fill(std::span<u8> out) {
#if defined(_WIN32)
    while (!out.empty()) {
        const std::size_t chunk = std::min<std::size_t>(out.size(), 1u << 30);
        if (BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(chunk), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            std::abort();
        out = out.subspan(chunk);
    }
#elif defined(__APPLE__)
    arc4random_buf(out.data(), out.size());
#else
    // getentropy serves at most 256 bytes per call.
    while (!out.empty()) {
        const std::size_t chunk = std::min<std::size_t>(out.size(), 256);
        if (getentropy(out.data(), chunk) != 0) std::abort();
        out = out.subspan(chunk);
    }
#endif
}

}  // namespace reboot
