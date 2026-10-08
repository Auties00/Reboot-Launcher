#pragma once

#include <cstddef>
#include <cstdint>

// libFuzzer's entry point. Each fuzz target defines it once; a target returns 0 and treats any
// broken invariant as a crash (std::abort), which libFuzzer and the replay driver both report.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace reboot::testing {

// main() for toolchains built without libFuzzer (GCC, and MSVC or Clang without REBOOT_LIBFUZZER):
// feeds every file named on the command line, or found under a named directory, to
// LLVMFuzzerTestOneInput, so the committed corpora run as regression tests in CI.
int replay_corpus_main(int argc, char** argv);

// Aborts with `what` on stderr when `condition` is false.
void fuzz_require(bool condition, const char* what) noexcept;

}  // namespace reboot::testing
