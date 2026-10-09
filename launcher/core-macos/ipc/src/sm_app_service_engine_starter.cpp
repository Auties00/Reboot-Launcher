#include "reboot/os_macos/ipc/sm_app_service_engine_starter.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "agent_service.hpp"
#include "engine_agent.hpp"
#include "engine_start_rules.hpp"
#include "launchctl_run.hpp"
#include "messages.hpp"
#include "pending_call.hpp"
#include "spawn_lock.hpp"

namespace reboot::os_macos::ipc {

struct SmAppServiceEngineStarter::PendingRegister {
    PendingCall call;
};

namespace {

enum class AgentReady : u8 { Yes, AwaitingUser };

// RequiresApproval wins; an agent that ended up enabled is ready even when register reported an error.
[[nodiscard]] Result<AgentReady> after_register(const Result<void>& registered) {
    switch (agent_status(kEngineAgentPlist)) {
        case AgentStatus::RequiresApproval: return AgentReady::AwaitingUser;
        case AgentStatus::Enabled: return AgentReady::Yes;
        case AgentStatus::NotRegistered:
        case AgentStatus::NotFound: break;
    }
    if (!registered) return std::unexpected(registered.error());
    return AgentReady::Yes;
}

}  // namespace

SmAppServiceEngineStarter::SmAppServiceEngineStarter(u32 uid, ports::CallerContext caller)
    : uid_(uid), caller_(std::move(caller)) {}

SmAppServiceEngineStarter::~SmAppServiceEngineStarter() = default;

Result<ports::StartResult> SmAppServiceEngineStarter::ensure_started(const NativePath& /*engine_exe*/,
                                                                     const DataRoot& root) {
    if (const std::optional<ports::StartResult> refused = refusal_before_start(caller_, root)) return *refused;

    if (pending_register_ || main_bundle_has_agent(kEngineAgentPlist)) {
        if (!pending_register_) {
            switch (agent_step(agent_status(kEngineAgentPlist))) {
                case AgentStep::Kickstart: break;
                case AgentStep::AwaitingUser: return ports::StartResult::AwaitingUser;
                case AgentStep::Register: {
                    auto pending = std::make_unique<PendingRegister>();
                    if (auto started =
                            pending->call.start([] { return agent_register(kEngineAgentPlist, kEngineAgentLabel); });
                        !started)
                        return std::unexpected(std::move(started.error()));
                    pending_register_ = std::move(pending);
                    break;
                }
            }
        }
        if (pending_register_) {
            std::optional<Result<void>> registered = pending_register_->call.wait_for(kAgentRegisterDeadline);
            // The register stays pending, and the next call awaits it instead of issuing another.
            if (!registered) return std::unexpected(agent_register_timed_out(kEngineAgentLabel, kAgentRegisterDeadline));
            pending_register_.reset();
            const Result<AgentReady> ready = after_register(*registered);
            if (!ready) return std::unexpected(ready.error());
            if (*ready == AgentReady::AwaitingUser) return ports::StartResult::AwaitingUser;
        }
    }

    const NativePath state_dir = root.root / "state";
    if (auto created = create_private_dirs(state_dir); !created) return std::unexpected(std::move(created.error()));
    const Result<posix::UniqueFd> spawn_lock = lock_exclusive(state_dir / "spawn.lock");
    if (!spawn_lock) return std::unexpected(spawn_lock.error());

    const std::array<std::string, 2> arguments{
        "kickstart", "gui/" + std::to_string(uid_) + "/" + std::string(kEngineAgentLabel)};
    const Result<LaunchctlRun> run = run_launchctl(arguments, kLaunchctlDeadline);
    if (!run) {
        return make_diag(ErrorDomain::Platform, kAgentKickstartFailed)
            .arg("label", kEngineAgentLabel)
            .cause(run.error())
            .fail();
    }
    return kickstart_outcome(*run, kEngineAgentLabel, kLaunchctlDeadline);
}

}  // namespace reboot::os_macos::ipc
