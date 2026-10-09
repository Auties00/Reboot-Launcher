#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <vector>

#include "reboot/components/app_entry.hpp"
#include "reboot/components/endpoint_override.hpp"
#include "reboot/components/payload_entry.hpp"
#include "reboot/components/runtime_entry.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::components {

// The body of the Ed25519-signed release manifest, schema VersionStreams::manifest_schema.
// Departures from update-mechanism's v2 schema:
// - app.payload_min is dropped: a payload is selected by an exact payload_abi match;
// - runtimes[].spike_passed_at is a release-pipeline gate checked before signing, so the parser
//   skips it;
// - app.kind has no appimage until a Linux GUI exists (linux-gui-sandbox-distribution).
struct ReleaseManifest {
    u32 schema = VersionStreams::manifest_schema;
    u64 serial = 0;
    std::chrono::system_clock::time_point expires_at;
    std::vector<AppEntry> apps;
    std::vector<PayloadEntry> payloads;
    std::vector<RuntimeEntry> runtimes;
    std::optional<EndpointOverride> endpoint;

    bool operator==(const ReleaseManifest&) const = default;
};

// Parses a body whose signature was already verified; tests/data/release_manifest.json shows the
// layout. Unknown keys are ignored, and an app or runtime for an os, arch or runtime kind this build
// cannot name, or a payload file of an unknown role, is skipped. Refuses, with
// components.manifest_malformed naming the field:
// - any schema but VersionStreams::manifest_schema (components.manifest_schema_unsupported);
// - an artifact without sha256, or a RemoteFile with no urls;
// - two app entries for one platform and channel, or an unknown app kind;
// - two payloads with one version, a payload role listed twice, or a payload without ClientDll;
// - a duplicate runtime id, a VcRedist with a platform, or another runtime without one;
// - an endpoint override with an empty host or port 0.
[[nodiscard]] Result<ReleaseManifest> parse_release_manifest(std::span<const u8> body);

}  // namespace reboot::components
