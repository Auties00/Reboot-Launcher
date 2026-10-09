#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "messages.hpp"
#include "pipe_stream.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/os_windows/ipc/named_pipe_connector.hpp"
#include "reboot/os_windows/ipc/named_pipe_listener.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/os_windows/ipc/windows_caller_context.hpp"
#include "reboot/testing/conformance_waiter.hpp"
#include "reboot/testing/port_conformance.hpp"
#include "reboot/testing/scratch_dir.hpp"
#include "reboot/testing/wall_clock_waiter.hpp"
#include "unique_handle.hpp"
#include "win32.hpp"

namespace {

using namespace rb;
using namespace rb::os_windows::ipc;

[[nodiscard]] std::wstring widen(const std::string& ascii) { return {ascii.begin(), ascii.end()}; }

[[nodiscard]] Diagnostic last_error(std::string_view call, const std::string& endpoint) {
    return make_diag(ErrorDomain::Platform, kPipeCallFailed)
        .arg("call", call)
        .arg("name", endpoint)
        .os(SystemError{SystemError::Origin::Host, static_cast<i64>(GetLastError())});
}

[[nodiscard]] std::optional<std::wstring> environment_variable(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return std::nullopt;
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), size));
    return value;
}

// A second local account the CI runner provides through REBOOT_CONFORMANCE_OTHER_USER and
// REBOOT_CONFORMANCE_OTHER_PASSWORD; without one the other-user checks are skipped.
[[nodiscard]] std::shared_ptr<UniqueHandle> other_user_token() {
    const std::optional<std::wstring> user = environment_variable(L"REBOOT_CONFORMANCE_OTHER_USER");
    const std::optional<std::wstring> password = environment_variable(L"REBOOT_CONFORMANCE_OTHER_PASSWORD");
    if (!user || !password) return nullptr;
    HANDLE token = nullptr;
    if (!LogonUserW(user->c_str(), L".", password->c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token)) return nullptr;
    return std::make_shared<UniqueHandle>(token);
}

struct Fixture {
    OsRandom random;
    testing::WallClockWaiter waiter;
    testing::ScratchDir scratch;
    PipeTrust trust;

    [[nodiscard]] testing::ConformanceEnv env() { return {waiter, scratch.path()}; }
    [[nodiscard]] std::string fresh_endpoint() { return ports::endpoint_name(trust.self(), random_token_hex(random, 8)); }
};

[[nodiscard]] Fixture make_fixture() {
    OsRandom random;
    auto scratch = testing::ScratchDir::create(random, "reboot-ipc-conformance");
    REQUIRE(scratch);
    auto trust = PipeTrust::for_current_process();
    REQUIRE(trust);
    return Fixture{{}, testing::WallClockWaiter{}, std::move(*scratch), std::move(*trust)};
}

}  // namespace

TEST_CASE("the named pipe adapters pass the IPC port suite", "[ipc]") {
    Fixture fixture = make_fixture();
    auto& random = fixture.random;
    const PipeTrust& trust = fixture.trust;

    testing::IpcConformanceSubject subject;
    subject.make_listener = [&trust] { return std::make_unique<NamedPipeListener>(trust); };
    subject.connector = std::make_unique<NamedPipeConnector>(trust);
    subject.make_endpoint = [&random, &trust] { return ports::endpoint_name(trust.self(), random_token_hex(random, 8)); };
    subject.self_user_id = trust.user_sid();

    auto squatters = std::make_shared<std::vector<UniqueHandle>>();
    if (auto token = other_user_token()) {
        subject.connect_as_other_user =
            [token](const std::string& endpoint) -> Result<std::unique_ptr<ports::IByteStream>> {
            if (!ImpersonateLoggedOnUser(token->get())) return std::unexpected(last_error("ImpersonateLoggedOnUser", endpoint));
            UniqueHandle pipe{CreateFileW(widen(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr)};
            const DWORD error = GetLastError();
            RevertToSelf();
            if (!pipe) {
                SetLastError(error);
                return std::unexpected(last_error("CreateFileW", endpoint));
            }
            auto stream = PipeStream::connected(std::move(pipe), ports::PeerIdentity{});
            if (!stream) return std::unexpected(std::move(stream.error()));
            return std::unique_ptr<ports::IByteStream>(std::move(*stream));
        };
        subject.squat_as_other_user = [token, squatters](const std::string& endpoint) -> Result<void> {
            if (!ImpersonateLoggedOnUser(token->get())) return std::unexpected(last_error("ImpersonateLoggedOnUser", endpoint));
            UniqueHandle pipe{CreateNamedPipeW(widen(endpoint).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 0, nullptr)};
            const DWORD error = GetLastError();
            RevertToSelf();
            if (!pipe) {
                SetLastError(error);
                return std::unexpected(last_error("CreateNamedPipeW", endpoint));
            }
            squatters->push_back(std::move(pipe));
            return {};
        };
    }

    const auto report = testing::run_ipc_conformance(std::move(subject), fixture.env());
    INFO(report.describe());
    CHECK(report.passed());
}

TEST_CASE("a second listener on a held name fails with platform.pipe_name_taken", "[ipc]") {
    Fixture fixture = make_fixture();
    const std::string endpoint = fixture.fresh_endpoint();
    NamedPipeListener first{fixture.trust};
    REQUIRE(first.listen(endpoint, [](std::unique_ptr<ports::IByteStream>) {}));

    NamedPipeListener second{fixture.trust};
    const auto squatted = second.listen(endpoint, [](std::unique_ptr<ports::IByteStream>) {});
    REQUIRE_FALSE(squatted);
    CHECK(squatted.error().is(kPipeNameTaken));
}

TEST_CASE("a client that will not be identified is dropped unread", "[ipc]") {
    Fixture fixture = make_fixture();
    const std::string endpoint = fixture.fresh_endpoint();
    std::atomic<int> accepted{0};
    NamedPipeListener listener{fixture.trust};
    REQUIRE(listener.listen(endpoint, [&accepted](std::unique_ptr<ports::IByteStream>) { ++accepted; }));

    UniqueHandle pipe{CreateFileW(widen(endpoint).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr)};
    REQUIRE(pipe);
    const bool dropped = fixture.waiter.wait_until(
        [&pipe] {
            DWORD available = 0;
            return PeekNamedPipe(pipe.get(), nullptr, 0, nullptr, &available, nullptr) == FALSE;
        },
        fixture.env().budget);
    CHECK(dropped);
    CHECK(accepted == 0);
}

TEST_CASE("the caller context is stable and interactive only outside session 0", "[ipc]") {
    auto probe = WindowsCallerContext::detect();
    REQUIRE(probe);
    const auto report = testing::run_caller_context_conformance(*probe, GetCurrentProcessId());
    INFO(report.describe());
    CHECK(report.passed());

    const ports::CallerContext context = probe->capture();
    if (context.interactive) CHECK(context.os_session != "0");
}
