#include "reboot/testing/scripted_process_launcher.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace rb::testing {
namespace {

[[nodiscard]] std::string file_name_of(const NativePath& exe) {
    const std::u8string name = exe.filename().u8string();
    return {name.begin(), name.end()};
}

struct Orphan {
    std::chrono::system_clock::time_point created;
    bool alive = true;
};

[[nodiscard]] Diagnostic no_such_process(u32 pid) {
    return make_diag(kTestingDomain, msg::kNoSuchProcess).arg("pid", pid).kind(ErrorKind::NotFound);
}

}  // namespace

struct ScriptedProcessLauncher::Impl {
    Impl(Executor& executor, const IClock& clock_ref, FakeOs fake_os) : io(executor), clock(clock_ref), os(fake_os) {}

    Executor& io;
    const IClock& clock;
    FakeOs os;
    mutable std::mutex mutex;
    // Rules are tried without the lock held, since one may spawn or inspect children itself.
    std::vector<SpawnRule> rules;
    std::vector<std::unique_ptr<ScriptedChild>> children;
    std::map<u32, Orphan> orphans;
    std::vector<u32> killed;
    u32 next_pid = 1000;

    [[nodiscard]] ports::ChildExit kill_exit() const {
        if (os == FakeOs::Windows) return {kJobKillExitCode, std::nullopt};
        return {std::nullopt, 9};
    }

    [[nodiscard]] ScriptedChild* child_of(u32 pid) const {
        for (const auto& child : children)
            if (child->pid() == pid) return child.get();
        return nullptr;
    }
};

ScriptedProcessLauncher::ScriptedProcessLauncher(Executor& io, const IClock& clock, FakeOs os)
    : impl_(std::make_unique<Impl>(io, clock, os)) {}

ScriptedProcessLauncher::~ScriptedProcessLauncher() = default;

Result<std::unique_ptr<ports::ChildProcess>> ScriptedProcessLauncher::spawn(const ports::ProcessLaunch& launch) {
    std::size_t index = 0;
    for (; index < impl_->rules.size(); ++index)
        if (impl_->rules[index].matches && impl_->rules[index].matches(launch)) break;
    if (index == impl_->rules.size())
        return make_diag(kTestingDomain, msg::kUnscriptedSpawn).arg("exe", launch.exe).kind(ErrorKind::NotFound).fail();

    u32 pid = 0;
    {
        const std::scoped_lock lock(impl_->mutex);
        pid = impl_->next_pid++;
    }
    auto child = std::make_unique<ScriptedChild>(impl_->io, pid, impl_->clock.system_now(), launch, impl_->kill_exit());
    // Moved out while it runs, so a rule added from inside it cannot invalidate it.
    UniqueFunction<Result<void>(ScriptedChild&)> on_spawn = std::move(impl_->rules[index].on_spawn);
    const bool once = impl_->rules[index].once;
    Result<void> spawned = on_spawn ? on_spawn(*child) : Result<void>{};
    if (once) {
        impl_->rules.erase(impl_->rules.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
        impl_->rules[index].on_spawn = std::move(on_spawn);
    }
    if (!spawned) return std::unexpected(std::move(spawned.error()));

    std::unique_ptr<ports::ChildProcess> handle = child->make_handle();
    const std::scoped_lock lock(impl_->mutex);
    impl_->children.push_back(std::move(child));
    return handle;
}

Result<bool> ScriptedProcessLauncher::is_alive(u32 pid, std::chrono::system_clock::time_point created) {
    const std::scoped_lock lock(impl_->mutex);
    if (const ScriptedChild* child = impl_->child_of(pid))
        return child->created() == created && !child->exited();
    if (const auto it = impl_->orphans.find(pid); it != impl_->orphans.end())
        return it->second.created == created && it->second.alive;
    return false;
}

Result<void> ScriptedProcessLauncher::kill(u32 pid, std::chrono::system_clock::time_point created) {
    ScriptedChild* target = nullptr;
    {
        const std::scoped_lock lock(impl_->mutex);
        if (ScriptedChild* child = impl_->child_of(pid); child != nullptr && child->created() == created && !child->exited()) {
            target = child;
        } else if (const auto it = impl_->orphans.find(pid);
                   it != impl_->orphans.end() && it->second.created == created && it->second.alive) {
            it->second.alive = false;
        } else {
            return std::unexpected(no_such_process(pid));
        }
        impl_->killed.push_back(pid);
    }
    if (target != nullptr) target->exit(impl_->kill_exit());
    return {};
}

void ScriptedProcessLauncher::add_rule(SpawnRule rule) { impl_->rules.push_back(std::move(rule)); }

void ScriptedProcessLauncher::on_exe(std::string_view file_name, UniqueFunction<Result<void>(ScriptedChild&)> on_spawn) {
    add_rule(SpawnRule{[name = std::string(file_name)](const ports::ProcessLaunch& launch) {
                           return file_name_of(launch.exe) == name;
                       },
                       std::move(on_spawn), false});
}

void ScriptedProcessLauncher::serve_exe(
    std::string_view file_name, UniqueFunction<std::unique_ptr<IStdioPeer>(const ports::ProcessLaunch&)> make_peer) {
    on_exe(file_name, [make = std::move(make_peer)](ScriptedChild& child) mutable -> Result<void> {
        child.attach_peer(make(child.launch()));
        return {};
    });
}

void ScriptedProcessLauncher::fail_exe(std::string_view file_name, Diagnostic error) {
    on_exe(file_name, [error = std::move(error)](ScriptedChild&) -> Result<void> { return std::unexpected(error); });
}

void ScriptedProcessLauncher::add_orphan(u32 pid, std::chrono::system_clock::time_point created) {
    const std::scoped_lock lock(impl_->mutex);
    impl_->orphans.insert_or_assign(pid, Orphan{created, true});
}

std::vector<ScriptedChild*> ScriptedProcessLauncher::children() const {
    const std::scoped_lock lock(impl_->mutex);
    std::vector<ScriptedChild*> out;
    out.reserve(impl_->children.size());
    for (const auto& child : impl_->children) out.push_back(child.get());
    return out;
}

ScriptedChild* ScriptedProcessLauncher::last(std::string_view file_name) const {
    const std::scoped_lock lock(impl_->mutex);
    for (auto it = impl_->children.rbegin(); it != impl_->children.rend(); ++it)
        if (file_name_of((*it)->launch().exe) == file_name) return it->get();
    return nullptr;
}

std::vector<u32> ScriptedProcessLauncher::killed() const {
    const std::scoped_lock lock(impl_->mutex);
    return impl_->killed;
}

}  // namespace rb::testing
