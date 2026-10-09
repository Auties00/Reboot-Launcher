#include "raw_stdio.hpp"

// Core code includes no OS header, so the POSIX calls are declared here, alone in this file.
#ifdef _WIN32
#include <io.h>
#else
extern "C" long read(int fd, void* buffer, unsigned long size);
extern "C" long write(int fd, const void* data, unsigned long size);
#endif

namespace rb::testing::raw_stdio {
namespace {

// Interrupted calls are retried; without errno here, a persistent error ends after these.
constexpr int kRetries = 64;
#ifdef _WIN32
// _O_BINARY, which lives in <fcntl.h>.
constexpr int kBinaryMode = 0x8000;
constexpr unsigned long kMaxChunk = 1u << 30;
#endif

}  // namespace

void make_binary() noexcept {
#ifdef _WIN32
    (void)_setmode(0, kBinaryMode);
    (void)_setmode(1, kBinaryMode);
    (void)_setmode(2, kBinaryMode);
#endif
}

long read_stdin(unsigned char* buffer, unsigned long size) noexcept {
    for (int attempt = 0; attempt < kRetries; ++attempt) {
#ifdef _WIN32
        const int got = _read(0, buffer, static_cast<unsigned int>(size < kMaxChunk ? size : kMaxChunk));
#else
        const long got = read(0, buffer, size);
#endif
        if (got >= 0) return got;
    }
    return 0;
}

bool write_all(int fd, const unsigned char* data, unsigned long size) noexcept {
    int failures = 0;
    while (size > 0) {
#ifdef _WIN32
        const int wrote = _write(fd, data, static_cast<unsigned int>(size < kMaxChunk ? size : kMaxChunk));
#else
        const long wrote = write(fd, data, size);
#endif
        if (wrote <= 0) {
            if (++failures == kRetries) return false;
            continue;
        }
        data += wrote;
        size -= static_cast<unsigned long>(wrote);
    }
    return true;
}

}  // namespace rb::testing::raw_stdio
