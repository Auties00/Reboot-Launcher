#include "reboot/compat/prefix_lease.hpp"

#include <utility>

#include "reboot/compat/prefix_manager.hpp"

namespace rb::compat {

PrefixLease::PrefixLease(PrefixManager& manager, u64 id, RunnerKind kind, SessionId session)
    : manager_(&manager), id_(id), kind_(kind), session_(session) {}

PrefixLease::PrefixLease(PrefixLease&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)), id_(other.id_), kind_(other.kind_), session_(other.session_) {}

PrefixLease& PrefixLease::operator=(PrefixLease&& other) noexcept {
    if (this != &other) {
        release();
        manager_ = std::exchange(other.manager_, nullptr);
        id_ = other.id_;
        kind_ = other.kind_;
        session_ = other.session_;
    }
    return *this;
}

PrefixLease::~PrefixLease() { release(); }

void PrefixLease::release() noexcept {
    if (PrefixManager* manager = std::exchange(manager_, nullptr)) manager->release(id_);
}

}  // namespace rb::compat
