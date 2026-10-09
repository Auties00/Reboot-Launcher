// Stands in for reboot-engine in the engine starter conformance: listens on the endpoint of
// REBOOT_LAUNCHER_HOME for a while, then exits.
#include <chrono>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "reboot/foundation/paths.hpp"
#include "reboot/os_windows/ipc/named_pipe_listener.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/ports/ipc.hpp"
#include "win32.hpp"

namespace {

constexpr std::chrono::seconds kLifetime{10};

[[nodiscard]] std::optional<std::wstring> variable(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return std::nullopt;
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), size));
    return value;
}

}  // namespace

// A stray REBOOT_ variable means the starter leaked the client's environment.
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || std::wstring_view{argv[1]} != L"run" || std::wstring_view{argv[2]} != L"--origin=on-demand") return 2;
    if (variable(L"REBOOT_CONFORMANCE_LEAK")) return 3;
    const std::optional<std::wstring> home = variable(L"REBOOT_LAUNCHER_HOME");
    if (!home) return 4;
    auto trust = reboot::os_windows::ipc::PipeTrust::for_current_process();
    if (!trust) return 5;
    const reboot::DataRoot root{reboot::NativePath{*home}, true};
    const std::string endpoint = reboot::ports::endpoint_name(trust->self(), reboot::root_hash16(reboot::canonical_root(root)));

    std::mutex mutex;
    std::vector<std::unique_ptr<reboot::ports::IByteStream>> clients;
    reboot::os_windows::ipc::NamedPipeListener listener{*trust};
    if (!listener.listen(endpoint, [&](std::unique_ptr<reboot::ports::IByteStream> stream) {
            const std::lock_guard lock{mutex};
            clients.push_back(std::move(stream));
        }))
        return 6;
    std::this_thread::sleep_for(kLifetime);
    return 0;
}
