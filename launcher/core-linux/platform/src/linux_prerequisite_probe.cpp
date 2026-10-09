#include "reboot/os_linux/platform/linux_prerequisite_probe.hpp"

#include <dlfcn.h>
#include <sys/stat.h>
#include <utility>

#include "helper_process.hpp"
#include "messages.hpp"
#include "process_environment.hpp"

namespace rb::os_linux::platform {

namespace {

[[nodiscard]] bool python3_runs() {
    const std::optional<NativePath> python = find_in_path("python3", env_value("PATH").value_or(""));
    if (!python) return false;
    HelperCommand command;
    command.program = python->native();
    command.args = {"-c", "pass"};
    const Result<HelperResult> ran = run_helper(command);
    return ran && ran->exit_code == 0;
}

[[nodiscard]] bool vulkan_loader_loads() {
    void* const library = ::dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr) return false;
    ::dlclose(library);
    return true;
}

[[nodiscard]] bool lingers(const std::string& user) {
    if (user.empty()) return false;
    const NativePath marker = NativePath{"/var/lib/systemd/linger"} / user;
    struct stat info {};
    return ::stat(marker.c_str(), &info) == 0;
}

[[nodiscard]] ports::PrerequisiteStatus status(std::string_view id, bool met, MessageId remediation) {
    ports::PrerequisiteStatus result;
    result.id = std::string(id);
    result.met = met;
    if (!met) result.remediation_message_id = remediation;
    return result;
}

}  // namespace

LinuxPrerequisiteProbe::LinuxPrerequisiteProbe(std::string user_name) : user_name_(std::move(user_name)) {}

std::vector<ports::PrerequisiteStatus> LinuxPrerequisiteProbe::check() {
    return {
        status(kPython3Id, python3_runs(), kPython3Missing),
        status(kVulkanLoaderId, vulkan_loader_loads(), kVulkanLoaderMissing),
        status(kLingerId, lingers(user_name_), kLingerDisabled),
    };
}

Result<void> LinuxPrerequisiteProbe::remediate(std::string_view id) {
    if (id != kLingerId || user_name_.empty())
        return make_diag(ErrorDomain::Platform, kNoRemediation).arg("id", id).kind(ErrorKind::Unsupported).fail();
    HelperCommand command;
    command.program = "loginctl";
    command.args = {"enable-linger", user_name_};
    const Result<HelperResult> ran = run_helper(command);
    if (!ran) return std::unexpected(ran.error());
    if (ran->exit_code != 0) return std::unexpected(helper_failed("loginctl", *ran));
    return {};
}

}  // namespace rb::os_linux::platform
