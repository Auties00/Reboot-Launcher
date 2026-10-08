#include "reboot/injection/inject_failed.hpp"

#include "messages.hpp"

namespace reboot::injection {

Diagnostic to_diagnostic(const InjectFailed& error) {
    Diagnostic diag = make_diag(ErrorDomain::Injection, msg::kInjectFailed)
                          .arg("path", error.path)
                          .arg("slot", error.slot)
                          .build();
    diag.os_error = error.os;
    return diag;
}

}  // namespace reboot::injection
