#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/runner.hpp"

namespace rb::posix {

// Covers no capability ids (decisions process-model, game-control-channel).
// The runners' winhost token files: <base>/ctl-XXXXXX/token. `base` must be this user's alone (a
// real directory owned by the effective uid with mode 0700) and is created when missing. Each
// token gets its own mkdtemp directory and a file opened O_CREAT|O_EXCL|O_NOFOLLOW with mode 0600.
class TokenFiles {
public:
    explicit TokenFiles(NativePath base) : base_(std::move(base)) {}

    // Blocking. posix.not_a_directory or posix.directory_not_private when `base` is not this
    // user's alone, else the failing call's posix.call_failed_on_path.
    [[nodiscard]] Result<std::unique_ptr<ports::ITokenFile>> write(std::string_view token) const;
    // Blocking. Removes every ctl-* entry of `base`; nothing when it is missing or not this user's alone.
    void sweep() const;

    [[nodiscard]] const NativePath& base() const noexcept { return base_; }

private:
    NativePath base_;
};

}  // namespace rb::posix
