#pragma once

#include <optional>
#include <variant>

#include "reboot/builds/build_layout.hpp"
#include "reboot/builds/file_finder.hpp"
#include "reboot/builds/needs_shipping_choice.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::builds {

using LayoutResolution = std::variant<BuildLayout, NeedsShippingChoice>;

// Capabilities: game-builds.file-search, game-builds.+4.
// One walk, after probing the known relative paths and one wrapper folder. Blocking; runs on the
// WorkerPool. A shipping exe in FortniteGame/Binaries/Win64 makes its parent^3 the root.
class LayoutResolver {
public:
    explicit LayoutResolver(FileFinder finder = FileFinder{}) noexcept : finder_(finder) {}

    // `chosen_shipping`, relative to `folder`, settles a NeedsShippingChoice. Fails with
    // builds.missing_shipping, shipping_not_found, not_a_directory, io or cancelled.
    [[nodiscard]] Result<LayoutResolution> resolve(const NativePath& folder,
                                                   const std::optional<NativePath>& chosen_shipping,
                                                   const CancelToken& token) const;

    // Stats each stored path; false means resolve() again.
    [[nodiscard]] bool still_valid(const BuildLayout& layout) const;

private:
    FileFinder finder_;
};

}  // namespace rb::builds
