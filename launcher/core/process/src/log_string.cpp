#include "reboot/process/log_string.hpp"

#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/text.hpp"
#include "reboot/process/built_env.hpp"
#include "reboot/process/process_spec.hpp"
#include "reboot/process/windows_command_line.hpp"

namespace rb::process {

namespace {

constexpr std::string_view kAuthPasswordKey = "-AUTH_PASSWORD=";

}  // namespace

std::string to_log_string(const ProcessSpec& spec) {
    std::vector<std::string> argv;
    argv.reserve(spec.args.size() + 1);
    argv.push_back(display_utf8(spec.exe));
    for (const std::string& arg : spec.args) {
        if (arg.size() >= kAuthPasswordKey.size() &&
            iequals_ascii(std::string_view(arg).substr(0, kAuthPasswordKey.size()), kAuthPasswordKey)) {
            argv.push_back(arg.substr(0, kAuthPasswordKey.size()) + std::string(kMasked));
        } else {
            argv.push_back(arg);
        }
    }
    return quote_windows_args(argv);
}

std::string to_log_string(const BuiltEnv& env) {
    std::string out;
    for (const auto& [name, value] : env.vars().vars) {
        out.append(name).append(1, '=');
        out.append(env.sensitive(name) ? kMasked : std::string_view(value));
        out.push_back('\n');
    }
    return out;
}

}  // namespace rb::process
