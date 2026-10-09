#include "reboot/engine/engine_lock.hpp"

#include <utility>

#include "messages.hpp"

namespace rb::engine {

Result<std::optional<EngineLock>> EngineLock::try_acquire(ports::IFileSystem& fs, const AppLayout& layout) {
    const NativePath path = layout.engine_lock();
    Result<ports::FileLock> lock = fs.lock_exclusive(path, false);
    if (lock) return std::optional<EngineLock>(EngineLock(std::move(*lock)));
    if (lock.error().kind == ErrorKind::Conflict) return std::optional<EngineLock>();
    return make_diag(ErrorDomain::Engine, msg::kLockFailed).arg("path", path).cause(std::move(lock.error())).fail();
}

}  // namespace rb::engine
