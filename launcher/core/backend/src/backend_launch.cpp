#include "reboot/backend/backend_launch.hpp"

#include <optional>
#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/process/env_builder.hpp"

namespace rb::backend {

namespace {

// The environment is UTF-8, so a path that does not survive the trip is refused rather than mangled.
[[nodiscard]] Result<std::string> exact_utf8(const NativePath& path) {
    std::string text = display_utf8(path);
    if (NativePath(std::u8string(text.begin(), text.end())) != path)
        return invalid_input(msg::kPathNotUtf8).arg("path", path).fail();
    return text;
}

}  // namespace

Result<process::ProcessSpec> make_backend_spec(const BackendLaunch& launch) {
    Result<std::string> data = exact_utf8(launch.data_dir);
    if (!data) return std::unexpected(std::move(data.error()));
    Result<std::string> content = exact_utf8(launch.content_dir);
    if (!content) return std::unexpected(std::move(content.error()));

    process::EnvBuilder env(launch.env_syntax);
    env.daemon_base(launch.user_environment)
        .channel(kBackendDataEnv, std::move(*data))
        .channel(kBackendContentEnv, std::move(*content));
    Result<process::BuiltEnv> built = std::move(env).build();
    if (!built) return std::unexpected(std::move(built.error()));

    process::ProcessSpec spec;
    spec.role = process::ChildRole::Backend;
    spec.exe = launch.exe;
    spec.args = {std::string(kBackendControlArg)};
    spec.env = std::move(*built);
    spec.cwd = launch.data_dir;
    spec.stdio = ports::StdioMode::ControlChannel;
    if (Result<void> valid = spec.validate(); !valid) return std::unexpected(std::move(valid.error()));
    return spec;
}

}  // namespace rb::backend
