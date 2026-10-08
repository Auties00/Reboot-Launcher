#pragma once

#include <utility>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_file_watcher.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_loopback_peer_inspector.hpp"
#include "reboot/testing/fake_os.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/fake_port_inspector.hpp"
#include "reboot/testing/fake_prereqs.hpp"
#include "reboot/testing/fake_quic_transport.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fake_registrar.hpp"
#include "reboot/testing/fake_resolver.hpp"
#include "reboot/testing/fake_runner_platform.hpp"
#include "reboot/testing/fake_secret_store.hpp"
#include "reboot/testing/fake_security_probe.hpp"
#include "reboot/testing/fake_session_host.hpp"
#include "reboot/testing/fake_shell.hpp"
#include "reboot/testing/fake_system_info.hpp"
#include "reboot/testing/fake_update_applier.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/in_memory_ipc.hpp"
#include "reboot/testing/scripted_process_launcher.hpp"

namespace reboot::testing {

class DeterministicRuntime;

struct FakePlatformOptions {
    FakeOs os = FakeOs::Windows;
    NativePath base = default_fake_root();
    ports::PeerIdentity self{"1000", 4242};
    u64 random_seed = 1;
};

// Covers no capability ids (decision testing-strategy).
// A complete PlatformServices of fakes on one DeterministicRuntime, with the data, cache and logs
// roots created, plus the HTTP and QUIC transports the engine builds itself.
class FakePlatform {
public:
    explicit FakePlatform(DeterministicRuntime& runtime, FakePlatformOptions options = {});
    ~FakePlatform();
    FakePlatform(const FakePlatform&) = delete;
    FakePlatform& operator=(const FakePlatform&) = delete;
    FakePlatform(FakePlatform&&) = delete;
    FakePlatform& operator=(FakePlatform&&) = delete;

    // For consumers that borrow the services, such as EngineRuntime.
    [[nodiscard]] ports::PlatformServices& services() noexcept { return services_; }
    // For consumers that own them, such as EngineHost. The typed views below then live only as
    // long as that consumer; call once.
    [[nodiscard]] ports::PlatformServices take() noexcept { return std::move(services_); }
    [[nodiscard]] const FakePlatformOptions& options() const noexcept { return options_; }

    [[nodiscard]] FakePlatformPaths& paths() noexcept { return *paths_; }
    [[nodiscard]] InMemoryFileSystem& fs() noexcept { return *fs_; }
    [[nodiscard]] FakeFileWatcher& watcher() noexcept { return *watcher_; }
    [[nodiscard]] FakeDiskInfo& disk() noexcept { return *disk_; }
    [[nodiscard]] FakeSecretStore& secrets() noexcept { return *secrets_; }
    [[nodiscard]] ScriptedProcessLauncher& processes() noexcept { return *processes_; }
    // Null unless os is Windows.
    [[nodiscard]] FakeSessionHost* session_host() noexcept { return session_host_; }
    // Null when os is Windows.
    [[nodiscard]] FakeRunnerPlatform* runner() noexcept { return runner_; }
    [[nodiscard]] FakePortInspector& port_inspector() noexcept { return *port_inspector_; }
    [[nodiscard]] FakeLoopbackPeerInspector& peer_inspector() noexcept { return *peer_inspector_; }
    [[nodiscard]] FakeResolver& resolver() noexcept { return *resolver_; }
    [[nodiscard]] InMemoryIpc& ipc() noexcept { return ipc_; }
    [[nodiscard]] FakeShell& shell() noexcept { return *shell_; }
    [[nodiscard]] FakeRegistrar& integration() noexcept { return *integration_; }
    [[nodiscard]] FakeSecurityProbe& security() noexcept { return *security_; }
    [[nodiscard]] FakePrereqs& prerequisites() noexcept { return *prerequisites_; }
    [[nodiscard]] FakeSystemInfo& system() noexcept { return *system_; }
    [[nodiscard]] FakeUpdateApplier& updater() noexcept { return *updater_; }
    [[nodiscard]] FakeRandom& random() noexcept { return *random_; }
    [[nodiscard]] FakeHttpTransport& http() noexcept { return http_; }
    [[nodiscard]] FakeQuicTransport& quic() noexcept { return quic_; }

private:
    FakePlatformOptions options_;
    InMemoryIpc ipc_;
    FakeHttpTransport http_;
    FakeQuicTransport quic_;
    ports::PlatformServices services_;
    // Views into services_.
    FakePlatformPaths* paths_ = nullptr;
    InMemoryFileSystem* fs_ = nullptr;
    FakeFileWatcher* watcher_ = nullptr;
    FakeDiskInfo* disk_ = nullptr;
    FakeSecretStore* secrets_ = nullptr;
    ScriptedProcessLauncher* processes_ = nullptr;
    FakeSessionHost* session_host_ = nullptr;
    FakeRunnerPlatform* runner_ = nullptr;
    FakePortInspector* port_inspector_ = nullptr;
    FakeLoopbackPeerInspector* peer_inspector_ = nullptr;
    FakeResolver* resolver_ = nullptr;
    FakeShell* shell_ = nullptr;
    FakeRegistrar* integration_ = nullptr;
    FakeSecurityProbe* security_ = nullptr;
    FakePrereqs* prerequisites_ = nullptr;
    FakeSystemInfo* system_ = nullptr;
    FakeUpdateApplier* updater_ = nullptr;
    FakeRandom* random_ = nullptr;
};

}  // namespace reboot::testing
