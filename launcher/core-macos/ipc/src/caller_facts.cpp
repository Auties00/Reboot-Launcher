#include "caller_facts.hpp"

#include <string>

namespace rb::os_macos::ipc {

ports::CallerContext caller_context_from(const CallerFacts& facts) {
    ports::CallerContext context;
    context.os_session = std::to_string(facts.audit_session_id);
    context.elevated = facts.euid == 0;
    context.interactive = facts.graphic_access;
    return context;
}

}  // namespace rb::os_macos::ipc
