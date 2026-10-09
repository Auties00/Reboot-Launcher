// For the data root gate: the package this executable runs from and the engine's default roots.
#include <cstdio>
#include <exception>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ports/platform_services.hpp"

int main() {
    using namespace reboot;
    try {
        Result<ports::PlatformServices> platform = ports::make_platform(ports::PlatformOptions{});
        if (!platform) {
            std::fprintf(stderr, "reboot-data-root-probe: %s\n", platform.error().id.c_str());
            return 2;
        }
        const ports::IPlatformPaths& paths = *platform->paths;
        const std::optional<NativePath> package = paths.velopack_package_dir();
        std::printf("package=%s\n", package ? display_utf8(*package).c_str() : "");
        int inside = 0;
        for (const auto& [name, root] : {std::pair{"data_root", paths.default_data_root()},
                                         std::pair{"cache_root", paths.default_cache_root()},
                                         std::pair{"logs_root", paths.default_logs_root()}}) {
            std::printf("%s=%s\n", name, display_utf8(root).c_str());
            if (package && is_inside(root, *package)) {
                std::printf("inside=%s\n", name);
                inside = 1;
            }
        }
        return inside;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "reboot-data-root-probe: internal.bug: %s\n", e.what());
        return 2;
    }
}
