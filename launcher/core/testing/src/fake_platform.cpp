#include "reboot/testing/fake_platform.hpp"

#include <memory>
#include <utility>
#include <vector>

#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_client_platform.hpp"

namespace rb::testing {
namespace {

// Room for any install a test stages, so free space only matters where a test lowers it.
constexpr u64 kFakeVolumeTotal = u64{1} << 40;
constexpr u64 kFakeVolumeFree = u64{1} << 39;

// Puts a new Fake into the port slot and returns the typed view.
template <class Fake, class Port, class... Args>
Fake* install(std::unique_ptr<Port>& slot, Args&&... args) {
    auto fake = std::make_unique<Fake>(std::forward<Args>(args)...);
    Fake* view = fake.get();
    slot = std::move(fake);
    return view;
}

[[nodiscard]] FakeSystemFacts system_facts(FakeOs os) {
    FakeSystemFacts facts;
    switch (os) {
        case FakeOs::Windows: facts.os = {"windows", "10.0", "19045", "x86_64"}; break;
        case FakeOs::MacOs: facts.os = {"macos", "14.5", "23F79", "arm64"}; break;
        case FakeOs::Linux: facts.os = {"linux", "6.8.0", "", "x86_64"}; break;
    }
    return facts;
}

[[nodiscard]] std::vector<ports::RunnerKind> runner_kinds(FakeOs os) {
    if (os == FakeOs::MacOs) return {ports::RunnerKind::MacRuntime};
    return {ports::RunnerKind::Umu, ports::RunnerKind::Wine};
}

}  // namespace

FakePlatform::FakePlatform(DeterministicRuntime& runtime, FakePlatformOptions options)
    : options_(std::move(options)),
      ipc_(runtime.strand(), options_.self),
      http_(runtime.strand(), runtime.clock()),
      quic_(runtime.strand()) {
    Executor& strand = runtime.strand();
    const FakeOs os = options_.os;

    paths_ = install<FakePlatformPaths>(services_.paths, options_.base);
    fs_ = install<InMemoryFileSystem>(services_.fs, runtime.clock());
    logs_ = install<InMemoryLogFileSystem>(services_.logs, runtime.clock());
    watcher_ = install<FakeFileWatcher>(services_.watcher, strand);
    watcher_->follow(*fs_);
    for (const NativePath& root : {paths_->default_data_root(), paths_->default_cache_root(), paths_->default_logs_root(),
                                   paths_->ipc_runtime_base(), paths_->exe_dir()})
        fs_->make_dir(root);
    (void)logs_->create_directories(paths_->default_logs_root());

    disk_ = install<FakeDiskInfo>(services_.disk);
    disk_->add_volume({options_.base.root_path(), "fake", "fakefs", kFakeVolumeFree, kFakeVolumeTotal});

    secrets_ = install<FakeSecretStore>(services_.secrets);
    processes_ = install<ScriptedProcessLauncher>(services_.processes, strand, runtime.clock(), os);
    if (os == FakeOs::Windows) {
        session_host_ = install<FakeSessionHost>(services_.session_host, strand);
    } else {
        runner_ = install<FakeRunnerPlatform>(services_.runner, runner_kinds(os));
    }
    port_inspector_ = install<FakePortInspector>(services_.ports);
    peer_inspector_ = install<FakeLoopbackPeerInspector>(services_.peer_inspector, os == FakeOs::Linux);
    resolver_ = install<FakeResolver>(services_.resolver, strand);
    services_.ipc_listener = ipc_.make_listener();
    shell_ = install<FakeShell>(services_.shell, fs_);
    integration_ = install<FakeRegistrar>(services_.integration);
    security_ = install<FakeSecurityProbe>(services_.security);
    prerequisites_ = install<FakePrereqs>(services_.prerequisites);
    system_ = install<FakeSystemInfo>(services_.system);
    system_->set(system_facts(os));
    updater_ = install<FakeUpdateApplier>(services_.updater, true, fs_);
    random_ = install<FakeRandom>(services_.random, options_.random_seed);
}

FakePlatform::~FakePlatform() = default;

FakeClientPlatform::FakeClientPlatform(InMemoryIpc& ipc, NativePath base) {
    platform_.connector = ipc.make_connector();
    starter_ = install<FakeEngineStarter>(platform_.starter);
    caller_ = install<FakeCallerContext>(platform_.caller);
    paths_ = install<FakePlatformPaths>(platform_.paths, std::move(base));
    files_ = install<InMemoryFileSystem>(platform_.revisions);
    platform_.self = ipc.self();
}

}  // namespace rb::testing
