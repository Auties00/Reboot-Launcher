// Older clients address the frozen bootstrap subset by the ids in contracts; the schema must keep them.
#include "reboot/api/v1/method_table.hpp"
#include "reboot/contracts/ipc.hpp"

namespace rb::api {

static_assert(kEngineStatus == contracts::ipc::kEngineStatus);
static_assert(kEngineDrain == contracts::ipc::kEngineDrain);
static_assert(kEngineShutdown == contracts::ipc::kEngineShutdown);
static_assert(kEngineRestartWhenIdle == contracts::ipc::kEngineRestartWhenIdle);
static_assert(kSessionsList == contracts::ipc::kSessionsList);
static_assert(kSessionsStop == contracts::ipc::kSessionsStop);
static_assert(contracts::ipc::kBootstrapMethodIds.size() == 6, "a new bootstrap method needs an assertion here");

}  // namespace rb::api
