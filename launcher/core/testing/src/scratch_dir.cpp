#include "reboot/testing/scratch_dir.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace rb::testing {
namespace {

constexpr int kAttempts = 8;

}  // namespace

Result<ScratchDir> ScratchDir::create(IRandom& random, std::string_view prefix) {
    std::error_code error;
    const NativePath base = std::filesystem::temp_directory_path(error);
    if (error)
        return make_diag(kTestingDomain, msg::kScratchDirFailed)
            .arg("path", "temp")
            .detail(error.message())
            .os(SystemError{SystemError::Origin::Host, error.value()})
            .fail();
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        NativePath path = base / (std::string(prefix) + "-" + random_token_hex(random, 8));
        // create_directory reports false for a name that already exists, so a clash just retries.
        if (std::filesystem::create_directory(path, error)) return ScratchDir(std::move(path));
        if (error) break;
    }
    return make_diag(kTestingDomain, msg::kScratchDirFailed)
        .arg("path", base)
        .detail(error ? error.message() : std::string("every candidate name exists"))
        .os(SystemError{SystemError::Origin::Host, error.value()})
        .fail();
}

ScratchDir::ScratchDir(ScratchDir&& other) noexcept : path_(std::exchange(other.path_, NativePath{})) {}

ScratchDir& ScratchDir::operator=(ScratchDir&& other) noexcept {
    if (this != &other) {
        if (!path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
        path_ = std::exchange(other.path_, NativePath{});
    }
    return *this;
}

ScratchDir::~ScratchDir() {
    if (path_.empty()) return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

}  // namespace rb::testing
