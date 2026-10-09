#pragma once

#include <any>
#include <optional>

#include "reboot/api/v1/requests.hpp"
#include "reboot/browser/server_row.hpp"
#include "reboot/engine/api_router.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/user_request.hpp"

// UserRequest payloads as reboot.api.v1 prompts, and API answers as the payload's answer type.
namespace rb::engine::requests {

// The row as the API shows it, with the library build that can join it and whether it is ours.
[[nodiscard]] api::ServerEntry server_entry(const ApiRouterDeps& deps, const browser::ServerRow& row);

[[nodiscard]] api::UserActionRequired user_action(const ApiRouterDeps& deps, const UserRequest& request);

// engine.answer_mismatch when `answer` is not what the request's payload accepts.
[[nodiscard]] Result<std::any> answer_for(const UserRequest& request, const api::RequestAnswer& answer);

}  // namespace rb::engine::requests
