#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::compat {

// One <prefix>/dosdevices/<letter>: link, with its target resolved to a canonical host path.
// `letter` is lowercase, as dosdevices spells it.
struct DosDevice {
    char letter{};
    NativePath target;

    bool operator==(const DosDevice&) const = default;
};

// Covers no capability ids (decision persistence-format-migration).
// Translates host paths to the Windows paths a prefix sees, through its dosdevices links,
// never through an assumed Z:. The library stores host paths only; this runs at launch. Lexical
// once loaded, so safe on any thread.
class PathMapper {
public:
    PathMapper() = default;
    explicit PathMapper(std::vector<DosDevice> devices);

    // Blocking. Links that do not resolve are skipped; compat.dosdevices_unreadable when the
    // directory cannot be listed.
    [[nodiscard]] static Result<PathMapper> load(const NativePath& prefix);

    // `host` must be absolute and canonical. The drive whose target is the longest prefix of it
    // wins. compat.path_not_mapped, or compat.path_not_utf8 for a name Windows cannot spell.
    [[nodiscard]] Result<std::u16string> to_windows(const NativePath& host) const;

    [[nodiscard]] const std::vector<DosDevice>& devices() const noexcept { return devices_; }

private:
    std::vector<DosDevice> devices_;
};

}  // namespace rb::compat
