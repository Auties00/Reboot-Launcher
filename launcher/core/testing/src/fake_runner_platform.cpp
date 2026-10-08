#include "reboot/testing/fake_runner_platform.hpp"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace reboot::testing {
namespace {

// ProcessLaunch carries arguments and variables as UTF-8 text.
[[nodiscard]] std::string utf8_of(const NativePath& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}  // namespace

std::vector<ports::RunnerKind> FakeRunnerPlatform::supported() const {
    const std::scoped_lock lock(mutex_);
    return supported_;
}

Result<ports::RuntimeLayout> FakeRunnerPlatform::layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) {
    if (auto error = faults_.take(RunnerOperation::Layout)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    const auto it = std::ranges::find(layouts_, kind, &std::pair<ports::RunnerKind, ports::RuntimeLayout>::first);
    if (it == layouts_.end())
        return make_diag(kTestingDomain, msg::kNoRuntimeLayout).arg("runner", kind).kind(ErrorKind::NotFound).fail();
    ports::RuntimeLayout layout = it->second;
    layout.root = dirs.runtime / layout.root;
    layout.entry = dirs.launcher.value_or(dirs.runtime) / layout.entry;
    return layout;
}

Result<void> FakeRunnerPlatform::post_extract(const NativePath& runtime_dir) {
    if (auto error = faults_.take(RunnerOperation::PostExtract)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    post_extracted_.push_back(runtime_dir);
    return {};
}

Result<ports::ProcessLaunch> FakeRunnerPlatform::runner_launch(const ports::RuntimeLayout& layout,
                                                               const NativePath& prefix, const NativePath& winhost_exe,
                                                               ports::EnvBlock base) {
    if (auto error = faults_.take(RunnerOperation::RunnerLaunch)) return std::unexpected(std::move(*error));
    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    launch.args = {utf8_of(winhost_exe)};
    launch.env = std::move(base);
    for (const auto& variable : layout.env) launch.env.vars.push_back(variable);
    launch.env.vars.emplace_back("WINEPREFIX", utf8_of(prefix));
    launch.stdio = ports::StdioMode::Capture;
    return launch;
}

Result<void> FakeRunnerPlatform::runtime_setup(const ports::RuntimeLayout&, CancelToken) {
    if (auto error = faults_.take(RunnerOperation::RuntimeSetup)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    ++runtime_setups_;
    return {};
}

std::optional<UserRequestKind> FakeRunnerPlatform::pending_prerequisite() {
    const std::scoped_lock lock(mutex_);
    return pending_;
}

void FakeRunnerPlatform::set_layout(ports::RunnerKind kind, ports::RuntimeLayout layout) {
    const std::scoped_lock lock(mutex_);
    const auto it = std::ranges::find(layouts_, kind, &std::pair<ports::RunnerKind, ports::RuntimeLayout>::first);
    if (it != layouts_.end()) {
        it->second = std::move(layout);
    } else {
        layouts_.emplace_back(kind, std::move(layout));
    }
}

void FakeRunnerPlatform::set_pending_prerequisite(std::optional<UserRequestKind> kind) {
    const std::scoped_lock lock(mutex_);
    pending_ = kind;
}

std::vector<NativePath> FakeRunnerPlatform::post_extracted() const {
    const std::scoped_lock lock(mutex_);
    return post_extracted_;
}

std::size_t FakeRunnerPlatform::runtime_setups() const {
    const std::scoped_lock lock(mutex_);
    return runtime_setups_;
}

}  // namespace reboot::testing
