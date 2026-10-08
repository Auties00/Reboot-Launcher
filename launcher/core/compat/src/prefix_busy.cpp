#include "reboot/compat/prefix_busy.hpp"

#include <string>

#include "messages.hpp"

namespace reboot::compat {

Diagnostic to_diagnostic(const PrefixBusy& error) {
    return make_diag(ErrorDomain::Compat, msg::kPrefixBusy)
        .arg("runner", runner_name(error.runner))
        .arg("holder", format_uuid(error.holder.value))
        .kind(ErrorKind::Conflict)
        .retryable()
        .build();
}

}  // namespace reboot::compat
