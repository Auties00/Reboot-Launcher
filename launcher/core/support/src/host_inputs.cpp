#include "reboot/support/host_inputs.hpp"

#include "messages.hpp"

namespace reboot::support {
namespace {

[[nodiscard]] bool inverted(const GameVersion& min, const GameVersion& max) noexcept {
    if (max.patch) return max < min;
    return min.major > max.major || (min.major == max.major && min.minor > max.minor);
}

}  // namespace

Result<HostInputs> host_inputs_from(const components::Sha256Digest& game_server_sha256,
                                    const contracts::game_server::GameServerDescription& description) {
    HostInputs inputs{.pin = {.game_server_sha256 = game_server_sha256}, .ranges = {}};
    inputs.ranges.reserve(description.supports.size());
    for (const contracts::game_server::VersionSupport& support : description.supports) {
        auto malformed = [&] {
            return make_diag(ErrorDomain::Support, msg::kMalformedServerDescription)
                .arg("min", support.version_min)
                .arg("max", support.version_max)
                .fail();
        };
        Result<GameVersion> min = GameVersion::parse(support.version_min);
        Result<GameVersion> max = GameVersion::parse(support.version_max);
        if (!min || !max || inverted(*min, *max)) return malformed();

        VersionRange range{.min = *min, .max = *max, .changelists = {}};
        range.changelists.reserve(support.cl_ranges.size());
        for (const contracts::game_server::ClRange& cl : support.cl_ranges) {
            if (cl.first > cl.last) return malformed();
            range.changelists.push_back({.first = {cl.first}, .last = {cl.last}});
        }
        inputs.ranges.push_back(std::move(range));
    }
    return inputs;
}

}  // namespace reboot::support
