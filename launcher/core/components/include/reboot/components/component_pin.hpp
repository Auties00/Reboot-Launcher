#pragma once

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::components {

class ComponentStore;

// Strand-only. Keeps one component version out of garbage collection until it is released or
// destroyed; a moved-from pin holds nothing. The store must outlive its pins.
class ComponentPin {
public:
    ComponentPin() = default;
    ComponentPin(ComponentPin&& other) noexcept;
    ComponentPin& operator=(ComponentPin&& other) noexcept;
    ComponentPin(const ComponentPin&) = delete;
    ComponentPin& operator=(const ComponentPin&) = delete;
    ~ComponentPin();

    [[nodiscard]] bool held() const noexcept { return store_ != nullptr; }
    [[nodiscard]] const ComponentRef& ref() const noexcept { return ref_; }
    [[nodiscard]] SessionId session() const noexcept { return session_; }

    void release() noexcept;

private:
    friend class ComponentStore;
    ComponentPin(ComponentStore& store, u64 id, ComponentRef ref, SessionId session);

    ComponentStore* store_ = nullptr;
    u64 id_ = 0;
    ComponentRef ref_;
    SessionId session_;
};

}  // namespace rb::components
