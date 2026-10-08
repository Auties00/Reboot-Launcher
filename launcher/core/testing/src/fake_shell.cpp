#include "reboot/testing/fake_shell.hpp"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace reboot::testing {

Result<void> FakeShell::open_url(std::string_view https_url) {
    if (auto error = faults_.take(ShellOperation::OpenUrl)) return std::unexpected(std::move(*error));
    if (!https_url.starts_with("https://"))
        return make_diag(kTestingDomain, msg::kHttpsOnly).arg("url", https_url).kind(ErrorKind::InvalidInput).fail();
    const std::scoped_lock lock(mutex_);
    opened_urls_.emplace_back(https_url);
    return {};
}

Result<void> FakeShell::open_path(const NativePath& path) {
    if (auto error = faults_.take(ShellOperation::OpenPath)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    opened_paths_.push_back(path);
    return {};
}

Result<void> FakeShell::reveal(const NativePath& path) {
    if (auto error = faults_.take(ShellOperation::Reveal)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    revealed_.push_back(path);
    return {};
}

Result<void> FakeShell::trash(const NativePath& path) {
    if (auto error = faults_.take(ShellOperation::Trash)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    trashed_.push_back(path);
    return {};
}

std::vector<std::string> FakeShell::opened_urls() const {
    const std::scoped_lock lock(mutex_);
    return opened_urls_;
}

std::vector<NativePath> FakeShell::opened_paths() const {
    const std::scoped_lock lock(mutex_);
    return opened_paths_;
}

std::vector<NativePath> FakeShell::revealed() const {
    const std::scoped_lock lock(mutex_);
    return revealed_;
}

std::vector<NativePath> FakeShell::trashed() const {
    const std::scoped_lock lock(mutex_);
    return trashed_;
}

}  // namespace reboot::testing
