#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ipc {

// Wipes every block before freeing it, so a vector that grows leaves no copy behind.
template <class T>
class WipingAllocator {
public:
    using value_type = T;

    WipingAllocator() noexcept = default;
    template <class U>
    WipingAllocator(const WipingAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) { return std::allocator<T>{}.allocate(n); }
    void deallocate(T* p, std::size_t n) noexcept {
        secure_wipe(p, n * sizeof(T));
        std::allocator<T>{}.deallocate(p, n);
    }

    template <class U>
    bool operator==(const WipingAllocator<U>&) const noexcept {
        return true;
    }
};

using WipedBytes = std::vector<u8, WipingAllocator<u8>>;

}  // namespace rb::ipc
