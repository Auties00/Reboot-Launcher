#pragma once

#include <optional>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::engine {

// Capabilities: os-integration.single-instance.
// state/engine.lock, held for the engine's whole life: one engine per (OS user, data root). The OS
// drops it when the process dies, so a crashed engine never blocks the next start. It is taken
// before the IPC endpoint exists, so only the holder ever replaces a stale socket.
class EngineLock {
public:
    // Blocking; startup step 2. nullopt: another engine holds it, and this one exits 0 so clients
    // reach the holder. Any other failure is engine.lock_failed with the port's error as cause.
    [[nodiscard]] static Result<std::optional<EngineLock>> try_acquire(ports::IFileSystem& fs,
                                                                       const AppLayout& layout);

    EngineLock(EngineLock&&) noexcept = default;
    EngineLock& operator=(EngineLock&&) noexcept = default;
    EngineLock(const EngineLock&) = delete;
    EngineLock& operator=(const EngineLock&) = delete;
    ~EngineLock() = default;

    [[nodiscard]] bool held() const noexcept { return lock_.held(); }

private:
    explicit EngineLock(ports::FileLock lock) noexcept : lock_(std::move(lock)) {}

    ports::FileLock lock_;
};

}  // namespace rb::engine
