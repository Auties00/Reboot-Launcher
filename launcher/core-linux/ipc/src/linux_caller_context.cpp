#include "reboot/os_linux/ipc/linux_caller_context.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include "caller_facts.hpp"

extern char** environ;

namespace rb::os_linux::ipc {
namespace {

[[nodiscard]] std::optional<std::string> read_proc_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) return std::nullopt;
    return text;
}

}  // namespace

LinuxCallerContext LinuxCallerContext::detect() {
    std::vector<std::string_view> environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) environment.emplace_back(*entry);
    const std::optional<std::string> session_id = read_proc_file("/proc/self/sessionid");
    const std::optional<std::string> login_uid = read_proc_file("/proc/self/loginuid");
    const CallerFacts facts{
        .environment = environment,
        .session_id = session_id ? std::optional<std::string_view>{*session_id} : std::nullopt,
        .login_uid = login_uid ? std::optional<std::string_view>{*login_uid} : std::nullopt,
        .euid = static_cast<u32>(::geteuid()),
        .uid = static_cast<u32>(::getuid()),
    };
    return LinuxCallerContext{caller_context_from(facts)};
}

void LinuxCallerContext::allow_foreground(u32 /*pid*/) {}

}  // namespace rb::os_linux::ipc
