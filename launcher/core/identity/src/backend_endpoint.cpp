#include "backend_endpoint.hpp"

#include <utility>

#include "messages.hpp"
#include "reboot/storage/backend_target.hpp"

namespace rb::identity {

Result<HostPort> normalize_backend_endpoint(const HostPort& endpoint) {
    storage::BackendTarget target;
    target.kind = storage::BackendKind::Local;
    target.local.endpoint = endpoint;
    return storage::BackendTarget::normalize(std::move(target)).transform([](storage::BackendTarget normalized) {
        return std::move(normalized.local.endpoint);
    });
}

std::string endpoint_text(const HostPort& endpoint) {
    std::string text = endpoint.host.find(':') == std::string::npos ? endpoint.host : "[" + endpoint.host + "]";
    if (endpoint.port) text += ":" + std::to_string(endpoint.port->value);
    return text;
}

Diagnostic empty_login(const HostPort& endpoint) {
    return make_diag(ErrorDomain::Identity, msg::kEmptyRemoteLogin)
        .kind(ErrorKind::InvalidInput)
        .arg("backend", endpoint_text(endpoint))
        .build();
}

}  // namespace rb::identity
