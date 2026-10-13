#pragma once

#include <optional>
#include <string>

#include "reboot/base/diag.hpp"
#include "reboot/base/types.hpp"
#include "reboot/foundation/net_types.hpp"

namespace rb::net {

enum class ResolveErrorCode : u8 { InvalidAddress, NotFound, NoIpv4Address, Timeout, Failed, Cancelled };

struct ResolveError {
    ResolveErrorCode code = ResolveErrorCode::Failed;
    std::string address;
    std::optional<AddressError> parse_error;
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const ResolveError& error);

}  // namespace rb::net
