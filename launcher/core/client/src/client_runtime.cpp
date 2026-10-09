#include "client_runtime.hpp"

#include <cstdlib>
#include <utility>

#include "abi_boundary.hpp"
#include "completion_latch.hpp"

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
    return create(std::move(*platform));
}

Result<std::unique_ptr<ClientRuntime>> ClientRuntime::create(ports::ClientPlatform platform) {
    if (!platform.connector || !platform.starter || !platform.caller || !platform.paths || !platform.revisions ||
        platform.self.user_id.empty())
        return std::unexpected(internal_bug("client_runtime.platform_incomplete"));
    return std::unique_ptr<ClientRuntime>{new ClientRuntime(std::move(platform))};
}

ClientRuntime::ClientRuntime(ports::ClientPlatform platform)
    : platform_(std::move(platform)),
      launcher_home_(launcher_home()),
      executor_thread_([this] { executor_.run(); }),
      link_thread_([this] { link_executor_.run(); }) {}

ClientRuntime::~ClientRuntime() { stop(); }

ClientDeps ClientRuntime::deps() {
    return ClientDeps{*platform_.connector, *platform_.starter, *platform_.caller, *platform_.paths,
                      *platform_.revisions, clock_,             executor_,         link_executor_,
                      platform_.self,       launcher_home_};
}

void ClientRuntime::stop() {
    executor_.stop();
    link_executor_.stop();
    if (executor_thread_.joinable()) executor_thread_.join();
    if (link_thread_.joinable()) link_thread_.join();
}

rb_status open_context(std::unique_ptr<ClientRuntime> runtime, ConnectSettings settings, rb_ctx** out) {
    auto context = ClientContext::create(runtime->deps(), std::move(settings));
    if (!context) return fail(context.error());

    CompletionLatch<Result<Connected>> latch;
    (*context)->connect([&latch](Result<Connected> connected) { latch.set(std::move(connected)); });
    Result<Connected> connected = latch.wait();
    if (!connected) {
        (*context)->close();
        runtime->stop();
        return fail(connected.error());
    }
    if (connected->image_warning) set_last_error(contracts::common::to_wire(*connected->image_warning));
    *out = new rb_ctx{std::move(runtime), std::move(*context)};
    return RB_OK;
}

}  // namespace reboot::client
