#include "reboot/testing/fuzz.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "reboot/foundation/native_path.hpp"

namespace reboot::testing {
namespace {

[[nodiscard]] bool replay_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", display_utf8(file).c_str());
        return false;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<std::uint8_t> input(bytes.begin(), bytes.end());
    (void)LLVMFuzzerTestOneInput(input.data(), input.size());
    return true;
}

}  // namespace

int replay_corpus_main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <corpus file or directory>...\n", argv[0]);
        return 2;
    }
    std::size_t replayed = 0;
    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path target(argv[i]);
        std::error_code error;
        if (std::filesystem::is_directory(target, error)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(target, error)) {
                if (!entry.is_regular_file()) continue;
                if (!replay_file(entry.path())) return 1;
                ++replayed;
            }
        } else if (!replay_file(target)) {
            return 1;
        } else {
            ++replayed;
        }
        if (error) {
            std::fprintf(stderr, "cannot list %s: %s\n", display_utf8(target).c_str(), error.message().c_str());
            return 1;
        }
    }
    // An empty corpus would pass while testing nothing.
    if (replayed == 0) {
        std::fprintf(stderr, "no corpus inputs found\n");
        return 1;
    }
    std::fprintf(stderr, "replayed %zu inputs\n", replayed);
    return 0;
}

void fuzz_require(bool condition, const char* what) noexcept {
    if (condition) return;
    std::fprintf(stderr, "fuzz invariant broken: %s\n", what);
    std::abort();
}

}  // namespace reboot::testing
