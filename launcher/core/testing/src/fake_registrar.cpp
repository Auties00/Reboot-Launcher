#include "reboot/testing/fake_registrar.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace reboot::testing {

Result<ports::IntegrationStatus> FakeRegistrar::status(ports::IntegrationKind kind) {
    if (auto error = faults_.take(RegistrarOperation::Status)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    const auto it = entries_.find(kind);
    return ports::IntegrationStatus{kind, it == entries_.end() ? ports::IntegrationState::Absent : it->second.state, {}};
}

Result<void> FakeRegistrar::apply(ports::IntegrationKind kind, const NativePath& exe) {
    if (auto error = faults_.take(RegistrarOperation::Apply)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    entries_.insert_or_assign(kind, Entry{ports::IntegrationState::Ours, exe});
    return {};
}

Result<void> FakeRegistrar::remove(ports::IntegrationKind kind) {
    if (auto error = faults_.take(RegistrarOperation::Remove)) return std::unexpected(std::move(*error));
    const std::scoped_lock lock(mutex_);
    entries_.erase(kind);
    return {};
}

void FakeRegistrar::set_state(ports::IntegrationKind kind, ports::IntegrationState state) {
    const std::scoped_lock lock(mutex_);
    entries_[kind].state = state;
}

std::optional<NativePath> FakeRegistrar::applied_exe(ports::IntegrationKind kind) const {
    const std::scoped_lock lock(mutex_);
    const auto it = entries_.find(kind);
    if (it == entries_.end()) return std::nullopt;
    return it->second.exe;
}

}  // namespace reboot::testing
