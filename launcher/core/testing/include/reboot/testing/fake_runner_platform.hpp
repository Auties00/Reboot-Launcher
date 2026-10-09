#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/testing/fault_plan.hpp"

namespace rb::testing {

enum class RunnerOperation : u8 { Layout, PostExtract, RunnerLaunch, RuntimeSetup, PrefixCommand };

// Covers no capability ids (decision testing-strategy).
// IRunnerPlatform for macOS- and Linux-shaped play tests. runner_launch builds `<entry> <winhost_exe>`
// with WINEPREFIX, so a ScriptedProcessLauncher rule on the entry's file name can start a FakeWinhost.
// prefix_command builds `<entry> wineboot -u`, `<entry> wineserver -k` or `<entry> <exe> <args>`.
class FakeRunnerPlatform final : public ports::IRunnerPlatform {
public:
    // {MacRuntime} for a macOS shape, {Umu, Wine} for a Linux one, as the real adapters report.
    explicit FakeRunnerPlatform(std::vector<ports::RunnerKind> supported) : supported_(std::move(supported)) {}

    [[nodiscard]] std::vector<ports::RunnerKind> supported() const override;
    Result<ports::RuntimeLayout> layout(ports::RunnerKind kind, const ports::RuntimeDirs& dirs) override;
    Result<void> post_extract(const NativePath& runtime_dir) override;
    Result<ports::ProcessLaunch> runner_launch(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                               const NativePath& winhost_exe, ports::EnvBlock base) override;
    Result<ports::ProcessLaunch> prefix_command(const ports::RuntimeLayout& layout, const NativePath& prefix,
                                                const ports::PrefixCommand& command, ports::EnvBlock base) override;
    Result<std::optional<std::string>> runtime_setup(const ports::RuntimeLayout& layout, CancelToken token) override;
    [[nodiscard]] std::optional<UserRequestKind> pending_prerequisite() override;

    // `layout.root` is relative to RuntimeDirs::runtime and `layout.entry` to the launcher when
    // given, as umu-run is; a kind without one fails with testing.no_runtime_layout.
    void set_layout(ports::RunnerKind kind, ports::RuntimeLayout layout);
    // RosettaInstall until cleared, as on Apple Silicon without Rosetta.
    void set_pending_prerequisite(std::optional<UserRequestKind> kind);
    // What runtime_setup reports; nullopt (the default) as off Linux.
    void set_setup_build(std::optional<std::string> build);

    [[nodiscard]] std::vector<NativePath> post_extracted() const;
    [[nodiscard]] std::size_t runtime_setups() const;
    [[nodiscard]] FaultPlan<RunnerOperation>& faults() noexcept { return faults_; }

private:
    mutable std::mutex mutex_;
    std::vector<ports::RunnerKind> supported_;
    std::vector<std::pair<ports::RunnerKind, ports::RuntimeLayout>> layouts_;
    std::optional<UserRequestKind> pending_;
    std::vector<NativePath> post_extracted_;
    std::size_t runtime_setups_ = 0;
    std::optional<std::string> setup_build_;
    FaultPlan<RunnerOperation> faults_;
};

}  // namespace rb::testing
