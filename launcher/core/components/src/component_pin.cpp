#include "reboot/components/component_pin.hpp"

#include <utility>

#include "reboot/components/component_store.hpp"

namespace reboot::components {

ComponentPin::ComponentPin(ComponentStore& store, u64 id, ComponentRef ref, SessionId session)
    : store_(&store), id_(id), ref_(std::move(ref)), session_(session) {}

ComponentPin::ComponentPin(ComponentPin&& other) noexcept
    : store_(std::exchange(other.store_, nullptr)),
      id_(std::exchange(other.id_, 0)),
      ref_(std::move(other.ref_)),
      session_(other.session_) {}

ComponentPin& ComponentPin::operator=(ComponentPin&& other) noexcept {
    if (this != &other) {
        release();
        store_ = std::exchange(other.store_, nullptr);
        id_ = std::exchange(other.id_, 0);
        ref_ = std::move(other.ref_);
        session_ = other.session_;
    }
    return *this;
}

ComponentPin::~ComponentPin() { release(); }

void ComponentPin::release() noexcept {
    if (store_ == nullptr) return;
    std::exchange(store_, nullptr)->unpin(id_);
}

}  // namespace reboot::components
