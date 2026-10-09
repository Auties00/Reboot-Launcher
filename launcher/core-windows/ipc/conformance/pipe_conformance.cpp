#include <catch2/catch_test_macros.hpp>

#include "win32.hpp"

#include <aclapi.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_windows/ipc/named_pipe_connector.hpp"
#include "reboot/os_windows/ipc/named_pipe_listener.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/os_windows/ipc/windows_caller_context.hpp"
#include "reboot/os_windows/ipc/windows_client_paths.hpp"
#include "reboot/os_windows/ipc/windows_engine_starter.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "spawn_lock.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"

namespace {

using namespace reboot;
using namespace reboot::os_windows::ipc;
using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kBudget{5000};

[[nodiscard]] std::wstring widen(const std::string& ascii) { return {ascii.begin(), ascii.end()}; }

[[nodiscard]] std::span<const u8> bytes_of(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

[[nodiscard]] PipeTrust trust() {
    auto trust = PipeTrust::for_current_process();
    REQUIRE(trust);
    return std::move(*trust);
}

[[nodiscard]] std::string fresh_endpoint(const PipeTrust& trust) {
    OsRandom random;
    return ports::endpoint_name(trust.self(), random_token_hex(random, 8));
}

struct Seen {
    std::mutex mutex;
    std::string bytes;
    int closes = 0;
};

void watch(ports::IByteStream& stream, const std::shared_ptr<Seen>& seen) {
    stream.on_read([seen](std::span<const u8> bytes) {
        const std::lock_guard lock{seen->mutex};
        seen->bytes.append(bytes.begin(), bytes.end());
    });
    stream.on_close([seen] {
        const std::lock_guard lock{seen->mutex};
        ++seen->closes;
    });
}

[[nodiscard]] bool eventually(UniqueFunction<bool()> condition) {
    testing::WallClockWaiter waiter;
    return waiter.wait_until(std::move(condition), kBudget);
}

// A listener with its accepted streams kept, and a connected client.
struct Link {
    explicit Link(const PipeTrust& pipe_trust) : listener(std::make_unique<NamedPipeListener>(pipe_trust)) {
        endpoint = fresh_endpoint(pipe_trust);
        REQUIRE(listener->listen(endpoint, [this](std::unique_ptr<ports::IByteStream> stream) {
            watch(*stream, server_seen);
            const std::lock_guard lock{mutex};
            server = std::move(stream);
        }));
        auto connected = NamedPipeConnector{pipe_trust}.connect(endpoint, 2s);
        REQUIRE(connected);
        client = std::move(*connected);
        watch(*client, client_seen);
        REQUIRE(eventually([this] {
            const std::lock_guard lock{mutex};
            return server != nullptr;
        }));
    }

    std::string endpoint;
    std::mutex mutex;
    std::unique_ptr<ports::IByteStream> server;
    std::shared_ptr<Seen> server_seen = std::make_shared<Seen>();
    std::unique_ptr<ports::IByteStream> client;
    std::shared_ptr<Seen> client_seen = std::make_shared<Seen>();
    std::unique_ptr<NamedPipeListener> listener;
};

[[nodiscard]] std::string received(const std::shared_ptr<Seen>& seen) {
    const std::lock_guard lock{seen->mutex};
    return seen->bytes;
}

[[nodiscard]] int closes(const std::shared_ptr<Seen>& seen) {
    const std::lock_guard lock{seen->mutex};
    return seen->closes;
}

[[nodiscard]] std::string sddl_of(const std::vector<u8>& descriptor, SECURITY_INFORMATION parts) {
    wchar_t* text = nullptr;
    REQUIRE(ConvertSecurityDescriptorToStringSecurityDescriptorW(const_cast<u8*>(descriptor.data()), SDDL_REVISION_1, parts,
                                                                 &text, nullptr));
    std::string narrow = to_utf8(text);
    LocalFree(text);
    return narrow;
}

}  // namespace

TEST_CASE("the pipe descriptor is owned by the user, open to the user and SYSTEM only, at medium integrity", "[pipe]") {
    const PipeTrust pipe_trust = trust();
    const auto descriptor = pipe_trust.pipe_security_descriptor();
    REQUIRE(descriptor);
    CHECK(sddl_of(*descriptor, OWNER_SECURITY_INFORMATION) == "O:" + pipe_trust.user_sid());
    const std::string dacl = sddl_of(*descriptor, DACL_SECURITY_INFORMATION);
    CHECK(dacl.starts_with("D:P"));
    CHECK(dacl.find(";;;" + pipe_trust.user_sid() + ")") != std::string::npos);
    CHECK(dacl.find(";;;SY)") != std::string::npos);
    CHECK(std::ranges::count(dacl, '(') == 2);
    CHECK(sddl_of(*descriptor, LABEL_SECURITY_INFORMATION) == "S:(ML;;NW;;;ME)");
}

TEST_CASE("the client verifies the engine as its own user without a warning", "[pipe]") {
    const PipeTrust pipe_trust = trust();
    Link link{pipe_trust};
    CHECK(link.client->peer().user_id == pipe_trust.user_sid());
    CHECK(link.client->peer().pid == GetCurrentProcessId());
    CHECK(link.server->peer().pid == GetCurrentProcessId());
}

TEST_CASE("bytes the engine wrote before closing still reach the client, then the close", "[pipe]") {
    Link link{trust()};
    link.server->write(bytes_of("goodbye"));
    link.server->close();
    CHECK(eventually([&] { return closes(link.client_seen) == 1; }));
    CHECK(received(link.client_seen) == "goodbye");
    CHECK(eventually([&] { return closes(link.server_seen) == 1; }));
}

TEST_CASE("bytes queued behind a write in flight still go out on close", "[pipe]") {
    Link link{trust()};
    std::string expected;
    for (int i = 0; i < 64; ++i) expected += "frame " + std::to_string(i) + ";";
    // From the engine's I/O thread, so no write completion runs before close().
    link.server->on_read([&link, expected](std::span<const u8>) {
        for (std::size_t at = 0; at < expected.size(); at = expected.find(';', at) + 1)
            link.server->write(bytes_of(std::string_view{expected}.substr(at, expected.find(';', at) + 1 - at)));
        link.server->close();
    });
    link.client->write(bytes_of("go"));
    CHECK(eventually([&] { return closes(link.client_seen) == 1; }));
    CHECK(received(link.client_seen) == expected);
}

TEST_CASE("no read refills the buffer while the read callback still holds it", "[pipe]") {
    Link link{trust()};
    ports::IByteStream& client = *link.client;
    auto later = std::make_shared<Seen>();
    std::atomic<bool> intact{false};
    std::atomic<bool> done{false};
    client.on_read([&, later](std::span<const u8> bytes) {
        const std::string before(bytes.begin(), bytes.end());
        link.server->write(bytes_of("SECOND"));
        Sleep(200);
        // Setting a new callback here must not start a read into the buffer `bytes` points at.
        client.on_read([later](std::span<const u8> next) {
            const std::lock_guard lock{later->mutex};
            later->bytes.append(next.begin(), next.end());
        });
        Sleep(200);
        intact = std::string(bytes.begin(), bytes.end()) == before;
        done = true;
    });
    link.server->write(bytes_of("first"));
    REQUIRE(eventually([&] { return done.load(); }));
    CHECK(intact);
    CHECK(eventually([&] { return received(later) == "SECOND"; }));
}

TEST_CASE("a write failing because the peer left still reads what the peer sent", "[pipe]") {
    const PipeTrust pipe_trust = trust();
    NamedPipeListener listener{pipe_trust};
    const std::string endpoint = fresh_endpoint(pipe_trust);
    std::mutex mutex;
    std::unique_ptr<ports::IByteStream> server;
    REQUIRE(listener.listen(endpoint, [&](std::unique_ptr<ports::IByteStream> stream) {
        const std::lock_guard lock{mutex};
        server = std::move(stream);
    }));
    auto connected = NamedPipeConnector{pipe_trust}.connect(endpoint, 2s);
    REQUIRE(connected);
    std::unique_ptr<ports::IByteStream> client = std::move(*connected);
    auto seen = std::make_shared<Seen>();
    client->on_close([seen] {
        const std::lock_guard lock{seen->mutex};
        ++seen->closes;
    });
    REQUIRE(eventually([&] {
        const std::lock_guard lock{mutex};
        return server != nullptr;
    }));
    server->write(bytes_of("bye"));
    server->close();
    client->write(bytes_of("late"));
    Sleep(300);
    CHECK(closes(seen) == 0);
    client->on_read([seen](std::span<const u8> bytes) {
        const std::lock_guard lock{seen->mutex};
        seen->bytes.append(bytes.begin(), bytes.end());
    });
    CHECK(eventually([&] { return received(seen) == "bye" && closes(seen) == 1; }));
}

TEST_CASE("writes after a close are dropped and a second close does nothing", "[pipe]") {
    Link link{trust()};
    link.client->close();
    link.client->write(bytes_of("late"));
    link.client->close();
    CHECK(eventually([&] { return closes(link.server_seen) == 1 && closes(link.client_seen) == 1; }));
    CHECK(received(link.server_seen).empty());
}

TEST_CASE("bytes sent before on_read is set wait for it", "[pipe]") {
    const PipeTrust pipe_trust = trust();
    NamedPipeListener listener{pipe_trust};
    const std::string endpoint = fresh_endpoint(pipe_trust);
    std::mutex mutex;
    std::unique_ptr<ports::IByteStream> server;
    REQUIRE(listener.listen(endpoint, [&](std::unique_ptr<ports::IByteStream> stream) {
        const std::lock_guard lock{mutex};
        server = std::move(stream);
    }));
    auto client = NamedPipeConnector{pipe_trust}.connect(endpoint, 2s);
    REQUIRE(client);
    (*client)->write(bytes_of("early"));
    REQUIRE(eventually([&] {
        const std::lock_guard lock{mutex};
        return server != nullptr;
    }));
    auto seen = std::make_shared<Seen>();
    watch(*server, seen);
    CHECK(eventually([&] { return received(seen) == "early"; }));
}

TEST_CASE("destroying the listener closes the streams it still serves", "[pipe]") {
    Link link{trust()};
    link.listener.reset();
    CHECK(eventually([&] { return closes(link.client_seen) == 1; }));
    CHECK(closes(link.server_seen) == 1);
    // The orphaned engine-side stream stays safe to use and to destroy.
    link.server->write(bytes_of("ignored"));
    link.server->close();
    link.server->on_close([] {});
    link.server.reset();
}

TEST_CASE("a client stream may be destroyed from its own read callback", "[pipe]") {
    Link link{trust()};
    auto holder = std::make_shared<std::unique_ptr<ports::IByteStream>>(std::move(link.client));
    std::atomic<bool> destroyed{false};
    (*holder)->on_read([holder, &destroyed](std::span<const u8>) {
        holder->reset();
        destroyed = true;
    });
    link.server->write(bytes_of("x"));
    CHECK(eventually([&] { return destroyed.load(); }));
    CHECK(eventually([&] { return closes(link.server_seen) == 1; }));
}

TEST_CASE("a pipe busy past the deadline is platform.pipe_connect_timed_out", "[pipe]") {
    const PipeTrust pipe_trust = trust();
    const std::string endpoint = fresh_endpoint(pipe_trust);
    const UniqueHandle only{CreateNamedPipeW(widen(endpoint).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 0, nullptr)};
    REQUIRE(only);
    const UniqueHandle occupant{
        CreateFileW(widen(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)};
    REQUIRE(occupant);
    const auto busy = NamedPipeConnector{pipe_trust}.connect(endpoint, 200ms);
    REQUIRE_FALSE(busy);
    CHECK(busy.error().is(kPipeConnectTimedOut));
    CHECK(busy.error().kind == ErrorKind::EngineUnavailable);
}

TEST_CASE("a pipe owned by someone else is refused with ipc.endpoint_untrusted", "[pipe]") {
    const PipeTrust pipe_trust = trust();
    const std::string endpoint = fresh_endpoint(pipe_trust);
    // Owned by Administrators, which only an elevated process may set; skipped otherwise.
    const std::wstring sddl = L"O:BAD:(A;;GA;;;WD)";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    REQUIRE(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const UniqueHandle foreign{CreateNamedPipeW(widen(endpoint).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 0, &attributes)};
    LocalFree(descriptor);
    if (!foreign) {
        SKIP("only an elevated process can make Administrators the owner");
    }
    const auto refused = NamedPipeConnector{pipe_trust}.connect(endpoint, 1s);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().is(kEndpointUntrusted));
    REQUIRE_FALSE(refused.error().causes.empty());
    CHECK(refused.error().causes.front().is(kPipeOwnerMismatch));
}

TEST_CASE("spawn.lock held elsewhere times out with platform.spawn_lock_timed_out", "[spawn_lock]") {
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-spawn-lock");
    REQUIRE(scratch);
    const NativePath path = scratch->path() / "state" / "spawn.lock";
    {
        auto held = SpawnLock::acquire(path, 1s);
        REQUIRE(held);
        const auto second = SpawnLock::acquire(path, 100ms);
        REQUIRE_FALSE(second);
        CHECK(second.error().is(kSpawnLockTimedOut));
    }
    CHECK(SpawnLock::acquire(path, 100ms));
}

TEST_CASE("the client paths come from LocalAppData and this module", "[paths]") {
    auto paths = WindowsClientPaths::detect();
    REQUIRE(paths);
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    CHECK(paths->exe_dir() == NativePath{exe}.parent_path().lexically_normal());
    CHECK(paths->default_data_root().filename() == "Reboot Launcher");
    CHECK(paths->default_cache_root() == paths->default_data_root() / "cache");
    CHECK(paths->default_logs_root() == paths->default_data_root() / "logs");
    CHECK(paths->ipc_runtime_base().empty());
    CHECK(paths->install_kind() == ports::InstallKind::Portable);
    CHECK_FALSE(paths->velopack_package_dir());
    CHECK(paths->default_data_root().is_absolute());
}

TEST_CASE("the Velopack layout needs current\\sq.version beside Update.exe", "[paths]") {
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-velopack");
    REQUIRE(scratch);
    const NativePath root = scratch->path() / "RebootLauncher";
    const NativePath current = root / "current";
    std::filesystem::create_directories(current);
    CHECK_FALSE(velopack_root_of(current));
    std::ofstream{root / "Update.exe"} << "x";
    CHECK_FALSE(velopack_root_of(current));
    std::ofstream{current / "sq.version"} << "x";
    CHECK(velopack_root_of(current) == root);
    CHECK(velopack_root_of(root / "CURRENT") == root);
    CHECK_FALSE(velopack_root_of(root));
}

#if defined(REBOOT_FAKE_PIPE_ENGINE_EXE)
TEST_CASE("the engine starter starts a detached engine on an overridden root", "[engine_starter]") {
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-engine-starter");
    REQUIRE(scratch);
    const PipeTrust pipe_trust = trust();
    const auto caller = WindowsCallerContext::detect();
    REQUIRE(caller);
    ports::CallerContext context = caller->capture();
    context.elevated = false;
    context.interactive = true;
    WindowsEngineStarter starter{pipe_trust.user_sid(), context};

    const DataRoot root{scratch->path() / "home", true};
    const std::string endpoint = ports::endpoint_name(pipe_trust.self(), root_hash16(canonical_root(root)));
    // The fake engine exits at once when a REBOOT_ variable reaches it.
    SetEnvironmentVariableW(L"REBOOT_CONFORMANCE_LEAK", L"1");
    testing::EngineStarterSubject subject{NativePath{REBOOT_FAKE_PIPE_ENGINE_EXE}, root, [&pipe_trust, endpoint] {
                                              return NamedPipeConnector{pipe_trust}.connect(endpoint, 200ms).has_value();
                                          }};
    testing::WallClockWaiter waiter;
    const auto report = testing::run_engine_starter_conformance(starter, std::move(subject), {waiter, scratch->path()});
    SetEnvironmentVariableW(L"REBOOT_CONFORMANCE_LEAK", nullptr);
    INFO(report.describe());
    CHECK(report.passed());
    CHECK(std::filesystem::exists(root.root / "state" / "spawn.lock"));
}
#endif
