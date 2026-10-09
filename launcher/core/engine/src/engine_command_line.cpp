#include "reboot/engine/engine_command_line.hpp"

#include <expected>
#include <optional>

#include "messages.hpp"

namespace rb::engine {

namespace {

constexpr std::string_view kOriginFlag = "--origin=";

[[nodiscard]] std::unexpected<Diagnostic> refuse(std::string_view argument) {
    return make_diag(ErrorDomain::Engine, msg::kBadCommandLine)
        .arg("argument", argument)
        .kind(ErrorKind::InvalidInput)
        .fail();
}

}  // namespace

Result<EngineCommandLine> parse_command_line(std::span<const std::string_view> args) {
    if (args.empty()) return refuse("");
    if (args.front() != "run") return refuse(args.front());

    EngineCommandLine command_line;
    bool foreground = false;
    std::optional<EngineOrigin> origin;
    for (const std::string_view arg : args.subspan(1)) {
        if (arg == "--foreground" && !foreground && !origin) {
            foreground = true;
        } else if (arg == "--resume" && !command_line.resume) {
            command_line.resume = true;
        } else if (arg.starts_with(kOriginFlag) && !foreground && !origin) {
            origin = parse_origin_flag(arg.substr(kOriginFlag.size()));
            if (!origin) return refuse(arg);
        } else {
            return refuse(arg);
        }
    }
    if (foreground) command_line.origin = EngineOrigin::Foreground;
    else if (origin) command_line.origin = *origin;
    return command_line;
}

}  // namespace rb::engine
