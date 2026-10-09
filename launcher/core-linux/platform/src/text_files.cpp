#include "text_files.hpp"

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace rb::os_linux::platform {

Result<std::string> read_text_file(const NativePath& path, std::size_t limit) {
    const posix::UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, path));
    std::string text;
    std::array<char, 8192> chunk{};
    while (text.size() < limit) {
        const ssize_t got = ::read(fd.get(), chunk.data(), chunk.size());
        if (got < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(posix::call_failed("read", errno, path));
        }
        if (got == 0) break;
        text.append(chunk.data(), static_cast<std::size_t>(got));
    }
    if (text.size() > limit) text.resize(limit);
    return text;
}

std::optional<std::string> try_read_text_file(const NativePath& path) {
    Result<std::string> text = read_text_file(path);
    if (!text) return std::nullopt;
    return std::move(*text);
}

}  // namespace rb::os_linux::platform
