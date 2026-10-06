#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "core/features.hpp"
#include "core/types.hpp"

namespace sb {

struct SlotHandle {
    u32 index = 0;
    u32 gen = 0;  // 0 is never issued, so a default handle is always stale

    constexpr bool operator==(const SlotHandle&) const = default;
    [[nodiscard]] constexpr u64 pack() const noexcept { return (u64{gen} << 32) | index; }
    [[nodiscard]] static constexpr SlotHandle unpack(u64 v) noexcept {
        return {static_cast<u32>(v), static_cast<u32>(v >> 32)};
    }
};

// Single-threaded generational slot map; stale handles resolve to nullptr.
template <class T>
class SlotMap {
public:
    template <class... Args>
    std::pair<SlotHandle, T*> emplace(Args&&... args) {
        u32 idx;
        if (free_.empty()) {
            idx = static_cast<u32>(slots_.size());
            slots_.emplace_back();
        } else {
            idx = free_.back();
            free_.pop_back();
        }
        Slot& s = slots_[idx];
        s.gen = s.gen + 1 == 0 ? 1 : s.gen + 1;
        s.value.emplace(std::forward<Args>(args)...);
        ++size_;
        return {SlotHandle{idx, s.gen}, &*s.value};
    }

    [[nodiscard]] T* get(SlotHandle h) noexcept {
        if (h.index >= slots_.size()) return nullptr;
        Slot& s = slots_[h.index];
        return s.gen == h.gen && s.value ? &*s.value : nullptr;
    }

    bool erase(SlotHandle h) {
        T* v = get(h);
        if (!v) return false;
        slots_[h.index].value.reset();
        free_.push_back(h.index);
        --size_;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    template <class F>
    void for_each(F&& f) {
        for (u32 i = 0; i < slots_.size(); ++i)
            if (slots_[i].value) f(SlotHandle{i, slots_[i].gen}, *slots_[i].value);
    }

private:
    struct Slot {
        std::optional<T> value;
        u32 gen = 0;
    };
    std::vector<Slot> slots_;
    std::vector<u32> free_;
    std::size_t size_ = 0;
};

}  // namespace sb
