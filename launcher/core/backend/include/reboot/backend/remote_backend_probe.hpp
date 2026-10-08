#pragma once

#include <memory>

#include "reboot/backend/backend_info.hpp"
#include "reboot/backend/backend_url.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"

namespace reboot::net {
class HttpClient;
}

namespace reboot::backend {

// Capabilities: auth-backend.ping.
// Strand-only. GET <origin>/reboot/v1/backend-info with HttpSmall limits and no retry. Any HTTP
// status means reachable; a body {impl: "reboot", api_version, ws_port} means Reboot, and an
// unknown api_version still counts as Reboot. Without a scheme it tries https, then http, whose
// refusal by HostTlsMemory comes back unchanged so the caller can ask ConfirmUnencryptedUpstream.
class RemoteBackendProbe {
public:
    explicit RemoteBackendProbe(net::HttpClient& http);
    ~RemoteBackendProbe();
    RemoteBackendProbe(const RemoteBackendProbe&) = delete;
    RemoteBackendProbe& operator=(const RemoteBackendProbe&) = delete;

    // `done` runs on the strand exactly once; an unreachable upstream fails with backend.unreachable.
    void probe(const BackendUrl& url, CancelToken token, UniqueFunction<void(Result<BackendInfo>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::backend
