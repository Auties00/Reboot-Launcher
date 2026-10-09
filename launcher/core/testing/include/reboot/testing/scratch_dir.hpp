#pragma once

#include <string_view>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/random.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// A fresh real directory under the system temp directory, removed with its contents on
// destruction. Only OS conformance and contract runs use one; unit tests use InMemoryFileSystem.
class ScratchDir {
public:
    [[nodiscard]] static Result<ScratchDir> create(IRandom& random, std::string_view prefix);

    ScratchDir(ScratchDir&& other) noexcept;
    ScratchDir& operator=(ScratchDir&& other) noexcept;
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    ~ScratchDir();

    [[nodiscard]] const NativePath& path() const noexcept { return path_; }

private:
    explicit ScratchDir(NativePath path) : path_(std::move(path)) {}

    NativePath path_;
};

}  // namespace rb::testing
