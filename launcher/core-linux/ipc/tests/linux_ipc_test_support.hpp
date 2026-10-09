#pragma once

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"

// Real-OS helpers: this package builds only on Linux, so these tests double as its conformance run.
namespace rb::os_linux::ipc::test {

inline constexpr std::chrono::milliseconds kBudget{10000};

// A scratch directory owned by us with mode 0700, as a runtime base must be.
[[nodiscard]] inline testing::ScratchDir make_private_scratch(std::string_view prefix) {
    OsRandom random;
    auto dir = testing::ScratchDir::create(random, prefix);
    REQUIRE(dir.has_value());
    REQUIRE(::chmod(dir->path().c_str(), 0700) == 0);
    return std::move(*dir);
}

[[nodiscard]] inline bool wait_for(UniqueFunction<bool()> condition) {
    testing::WallClockWaiter waiter;
    return waiter.wait_until(std::move(condition), kBudget);
}

[[nodiscard]] inline u32 own_uid() noexcept { return static_cast<u32>(::geteuid()); }

[[nodiscard]] inline u32 mode_of(const NativePath& path) {
    struct stat info {};
    REQUIRE(::lstat(path.c_str(), &info) == 0);
    return static_cast<u32>(info.st_mode) & 07777U;
}

[[nodiscard]] inline bool path_exists(const NativePath& path) noexcept {
    struct stat info {};
    return ::lstat(path.c_str(), &info) == 0;
}

[[nodiscard]] inline std::optional<std::string> read_text(const NativePath& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline void write_text(const NativePath& path, std::string_view text, mode_t mode = 0600) {
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file << text;
    }
    REQUIRE(::chmod(path.c_str(), mode) == 0);
}

// Sets or unsets one environment variable and restores it on destruction.
class EnvOverride {
public:
    EnvOverride(const char* name, std::optional<std::string_view> value) : name_(name) {
        if (const char* previous = std::getenv(name)) previous_ = previous;
        if (value)
            REQUIRE(::setenv(name, std::string(*value).c_str(), 1) == 0);
        else
            REQUIRE(::unsetenv(name) == 0);
    }
    ~EnvOverride() {
        if (previous_)
            ::setenv(name_, previous_->c_str(), 1);
        else
            ::unsetenv(name_);
    }
    EnvOverride(const EnvOverride&) = delete;
    EnvOverride& operator=(const EnvOverride&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

}  // namespace rb::os_linux::ipc::test
