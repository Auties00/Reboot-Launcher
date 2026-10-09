#pragma once

// The fake executables' unbuffered standard streams. Plain types only, so raw_stdio.cpp can
// declare the POSIX calls itself without any header that might declare them too.
namespace rb::testing::raw_stdio {

// Binary stdin, stdout and stderr on Windows; nothing elsewhere.
void make_binary() noexcept;
// Blocks until something arrives: the count read, 0 at end of input or on an error.
long read_stdin(unsigned char* buffer, unsigned long size) noexcept;
// Writes all of it to fd 1 or 2; false once the reader went away.
bool write_all(int fd, const unsigned char* data, unsigned long size) noexcept;

}  // namespace rb::testing::raw_stdio
