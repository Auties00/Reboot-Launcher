#include <optional>
#include <string_view>

#include "reboot/os_windows/winhost/winhost_agent.hpp"

// The token file is winhost's only argument.
int wmain(int argc, wchar_t* argv[]) {
    const std::optional<std::wstring_view> token_file =
        argc == 2 ? std::optional<std::wstring_view>{argv[1]} : std::nullopt;
    return static_cast<int>(rb::os_windows::winhost::run_winhost(token_file));
}
