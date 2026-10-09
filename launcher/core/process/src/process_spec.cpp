#include "reboot/process/process_spec.hpp"

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::process {

NativePath ProcessSpec::working_directory() const { return cwd ? *cwd : exe.parent_path(); }

Result<void> ProcessSpec::validate() const {
    if (!exe.is_absolute())
        return make_diag(ErrorDomain::Process, msg::kSpecRelativePath).arg("path", exe).kind(ErrorKind::InvalidInput).fail();
    if (cwd && !cwd->is_absolute())
        return make_diag(ErrorDomain::Process, msg::kSpecRelativePath).arg("path", *cwd).kind(ErrorKind::InvalidInput).fail();
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.empty())
            return make_diag(ErrorDomain::Process, msg::kSpecEmptyArgument)
                .arg("index", i)
                .arg("program", exe.filename())
                .kind(ErrorKind::InvalidInput)
                .fail();
        if (arg.find('\0') != std::string::npos || !is_valid_utf8(arg))
            return make_diag(ErrorDomain::Process, msg::kSpecInvalidArgument)
                .arg("index", i)
                .arg("program", exe.filename())
                .kind(ErrorKind::InvalidInput)
                .fail();
    }
    return {};
}

WipingLaunch ProcessSpec::to_launch() const {
    ports::ProcessLaunch launch;
    launch.exe = exe;
    launch.args = args;
    launch.env = env.copy();
    launch.cwd = working_directory();
    launch.stdio = stdio;
    launch.own_group = true;
    launch.scope_name = scope_name;
    return WipingLaunch(std::move(launch));
}

}  // namespace rb::process
