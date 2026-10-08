#include "client_runtime.hpp"

#include <cstdlib>
#include <utility>

namespace reboot::client {

namespace {

[[nodiscard]] std::optional<std::string> launcher_home() {
    const char* value = std::getenv("REBOOT_LAUNCHER_HOME");
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string{value};
}

}  // namespace

Result<std::unique_ptr<ClientRuntime>> ClientRuntime::create() {
    auto platform = ports::make_client_platform();
    if (!platform) return std::unexpected(std::move(platform.error()));
    // Blocked until ports::ClientPlatform carries the file system and the caller's PeerIdentity.
    return std::unexpected(internal_bug("client_runtime.platform_incomplete"));
}

ClientRuntime::ClientRuntime(ports::ClientPlatform platform, std::unique_ptr<ports::IFileSystem> files,
                             ports::PeerIdentity self)
    : platform_(std::move(platform)),
      files_(std::move(files)),
      self_(std::move(self)),
      launcher_home_(launcher_home()),
      executor_thread_([this] { executor_.run(); }) {}

ClientRuntime::~ClientRuntime() { stop(); }

ClientDeps ClientRuntime::deps() {
    return ClientDeps{*platform_.connector, *platform_.starter, *platform_.caller, *platform_.paths, *files_,
                      clock_,               executor_,          self_,             launcher_home_};
}

void ClientRuntime::stop() {
    if (!executor_thread_.joinable()) return;
    executor_.stop();
    executor_thread_.join();
}

}  // namespace reboot::client
