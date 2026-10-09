#include "reboot/browser/view_spec.hpp"

#include <algorithm>

#include "messages.hpp"
#include "reboot/browser/version_match.hpp"
#include "wire/messages.hpp"

namespace reboot::browser {

namespace {

[[nodiscard]] std::unexpected<Diagnostic> invalid(std::string_view field) {
    return make_diag(ErrorDomain::Browser, kInvalidViewSpec).arg("field", field).kind(ErrorKind::InvalidInput).fail();
}

}  // namespace

Result<void> ViewSpec::validate() const {
    if (window == 0) return invalid("window");
    // Bucket 0 already means every version, so it never appears in a list of buckets.
    if (std::ranges::find(versions.buckets, sb::wire::kBucketAll) != versions.buckets.end()) return invalid("versions");
    if (versions.exact_version && !versions.buckets.empty())
        for (const u32 bucket : buckets_for(*versions.exact_version))
            if (std::ranges::find(versions.buckets, bucket) == versions.buckets.end()) return invalid("exact_version");
    return {};
}

}  // namespace reboot::browser
