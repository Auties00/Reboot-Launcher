#include "reboot/testing/fake_runner_platform.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/function.hpp"

namespace rb::testing {
namespace {

// ProcessLaunch carries arguments and variables as UTF-8 text.
[[nodiscard]] std::string utf8_of(const NativePath& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

}  // namespace

// The in-memory file, erased when the handle goes.
class FakeTokenFile final : public ports::ITokenFile {
public:
    FakeTokenFile(NativePath path, UniqueFunction<void()> erase) : path_(std::move(path)), erase_(std::move(erase)) {}
    ~FakeTokenFile() override { erase_(); }
    FakeTokenFile(const FakeTokenFile&) = delete;
    FakeTokenFile& operator=(const FakeTokenFile&) = delete;

    [[nodiscard]] const NativePath& path() const override { return path_; }

private:
    NativePath path_;
    UniqueFunction<void()> erase_;
};

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

Result<ports::ProcessLaunch> FakeRunnerPlatform::prefix_command(const ports::RuntimeLayout& layout,
                                                                const NativePath& prefix,
                                                                const ports::PrefixCommand& command,
                                                                ports::EnvBlock base) {
    if (auto error = faults_.take(RunnerOperation::PrefixCommand)) return std::unexpected(std::move(*error));
    ports::ProcessLaunch launch;
    launch.exe = layout.entry;
    switch (command.verb) {
        case ports::PrefixVerb::Boot: launch.args = {"wineboot", "-u"}; break;
        case ports::PrefixVerb::KillServer: launch.args = {"wineserver", "-k"}; break;
        case ports::PrefixVerb::Run:
            launch.args = {utf8_of(command.exe)};
            launch.args.insert(launch.args.end(), command.args.begin(), command.args.end());
            launch.cwd = command.exe.parent_path();
            break;
    }
    launch.env = std::move(base);
    launch.env.vars.emplace_back("WINEPREFIX", utf8_of(prefix));
    launch.stdio = ports::StdioMode::Capture;
    return launch;
}

Result<std::optional<std::string>> FakeRunnerPlatform::runtime_setup(const ports::RuntimeLayout&, CancelToken) {
    if (auto error = faults_.take(RunnerOperation::RuntimeSetup)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    ++runtime_setups_;
    return setup_build_;
}

std::optional<UserRequestKind> FakeRunnerPlatform::pending_prerequisite() {
    const std::scoped_lock lock(mutex_);
    return pending_;
}

Result<std::unique_ptr<ports::ITokenFile>> FakeRunnerPlatform::write_token_file(std::string_view token) {
    if (auto error = faults_.take(RunnerOperation::TokenFile)) return std::unexpected(std::move(*error));
    NativePath path;
    {
        const std::scoped_lock lock(mutex_);
        const NativePath base =
            token_base_.empty() ? std::filesystem::current_path().root_path() / "tmp" / "reboot" : token_base_;
        path = base / ("ctl-" + std::to_string(++token_count_)) / "token";
    }
    {
        const std::scoped_lock lock(tokens_->mutex);
        tokens_->files.emplace(path, std::string(token));
    }
    std::weak_ptr<TokenStore> weak = tokens_;
    return std::unique_ptr<ports::ITokenFile>(std::make_unique<FakeTokenFile>(path, [weak, path] {
        if (const auto store = weak.lock()) {
            const std::scoped_lock lock(store->mutex);
            store->files.erase(path);
        }
    }));
}

void FakeRunnerPlatform::sweep_token_files() {
    {
        const std::scoped_lock lock(mutex_);
        ++token_sweeps_;
    }
    const std::scoped_lock lock(tokens_->mutex);
    tokens_->files.clear();
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

void FakeRunnerPlatform::set_setup_build(std::optional<std::string> build) {
    const std::scoped_lock lock(mutex_);
    setup_build_ = std::move(build);
}

std::vector<NativePath> FakeRunnerPlatform::post_extracted() const {
    const std::scoped_lock lock(mutex_);
    return post_extracted_;
}

std::size_t FakeRunnerPlatform::runtime_setups() const {
    const std::scoped_lock lock(mutex_);
    return runtime_setups_;
}

void FakeRunnerPlatform::set_token_base(NativePath base) {
    const std::scoped_lock lock(mutex_);
    token_base_ = std::move(base);
}

std::vector<NativePath> FakeRunnerPlatform::token_files() const {
    const std::scoped_lock lock(tokens_->mutex);
    std::vector<NativePath> out;
    for (const auto& [path, text] : tokens_->files) out.push_back(path);
    return out;
}

std::optional<std::string> FakeRunnerPlatform::token_file_text(const NativePath& path) const {
    const std::scoped_lock lock(tokens_->mutex);
    const auto found = tokens_->files.find(path);
    if (found == tokens_->files.end()) return std::nullopt;
    return found->second;
}

std::size_t FakeRunnerPlatform::token_sweeps() const {
    const std::scoped_lock lock(mutex_);
    return token_sweeps_;
}

}  // namespace rb::testing
