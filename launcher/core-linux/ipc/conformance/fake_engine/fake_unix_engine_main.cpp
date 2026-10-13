// Stands in for reboot-engine in the engine starter conformance: takes state/engine.lock as an
// OFD lock and listens on the endpoint of REBOOT_LAUNCHER_HOME for a while, then exits.
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "reboot/foundation/paths.hpp"
#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"
#include "reboot/os_linux/ipc/unix_socket_listener.hpp"
#include "reboot/ports/ipc.hpp"
#include "state_locks.hpp"

namespace {

constexpr std::chrono::seconds kLifetime{10};

}  // namespace

// A stray REBOOT_ variable means the starter leaked the client's environment.
int main(int argc, char** argv) {
    if (argc != 3 || std::string_view{argv[1]} != "run" || std::string_view{argv[2]} != "--origin=on-demand") return 2;
    if (std::getenv("REBOOT_CONFORMANCE_LEAK") != nullptr) return 3;
    const char* const home = std::getenv("REBOOT_LAUNCHER_HOME");
    if (home == nullptr) return 4;
    const rb::DataRoot root{rb::NativePath{home}, true};
    const auto engine_lock = rb::os_linux::ipc::lock_ofd_exclusive(root.root / "state" / "engine.lock");
    if (!engine_lock) return 5;

    const auto uid = static_cast<rb::u32>(::geteuid());
    const rb::ports::PeerIdentity self{std::to_string(uid), static_cast<rb::u32>(::getpid())};
    const std::string endpoint = rb::ports::endpoint_name(self, rb::root_hash16(rb::canonical_root(root)));
    std::mutex mutex;
    std::vector<std::unique_ptr<rb::ports::IByteStream>> clients;
    rb::os_linux::ipc::UnixSocketListener listener{rb::os_linux::ipc::linux_ipc_runtime_base(uid).path};
    if (!listener.listen(endpoint, [&](std::unique_ptr<rb::ports::IByteStream> stream) {
            const std::lock_guard lock{mutex};
            clients.push_back(std::move(stream));
        }))
        return 6;
    std::this_thread::sleep_for(kLifetime);
    return 0;
}
