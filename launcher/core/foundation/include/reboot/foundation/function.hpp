#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace reboot {

template <class Signature>
class UniqueFunction;

// Move-only type-erased callable; stands in for std::move_only_function, which not every
// toolchain on the floor ships.
template <class R, class... Args>
class UniqueFunction<R(Args...)> {
    static constexpr std::size_t kInlineSize = 3 * sizeof(void*);

    struct VTable {
        R (*invoke)(void* storage, Args&&... args);
        void (*relocate)(void* to, void* from) noexcept;
        void (*destroy)(void* storage) noexcept;
    };

    template <class F>
    static constexpr bool kStoredInline = sizeof(F) <= kInlineSize && alignof(F) <= alignof(void*) &&
                                          std::is_nothrow_move_constructible_v<F>;

    template <class F>
    struct InlineOps {
        static F& get(void* s) noexcept { return *std::launder(static_cast<F*>(s)); }
        static R invoke(void* s, Args&&... args) { return std::invoke_r<R>(get(s), std::forward<Args>(args)...); }
        static void relocate(void* to, void* from) noexcept {
            ::new (to) F(std::move(get(from)));
            get(from).~F();
        }
        static void destroy(void* s) noexcept { get(s).~F(); }
        static constexpr VTable table{&invoke, &relocate, &destroy};
    };

    template <class F>
    struct HeapOps {
        static F*& get(void* s) noexcept { return *std::launder(static_cast<F**>(s)); }
        static R invoke(void* s, Args&&... args) { return std::invoke_r<R>(*get(s), std::forward<Args>(args)...); }
        static void relocate(void* to, void* from) noexcept { ::new (to) F*(get(from)); }
        static void destroy(void* s) noexcept { delete get(s); }
        static constexpr VTable table{&invoke, &relocate, &destroy};
    };

public:
    UniqueFunction() noexcept = default;
    UniqueFunction(std::nullptr_t) noexcept {}

    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, UniqueFunction> &&
                 std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
    UniqueFunction(F&& f) {
        using D = std::decay_t<F>;
        // A function reference is never null, and GCC rejects comparing one with -Werror.
        if constexpr (std::is_pointer_v<std::remove_cvref_t<F>> || std::is_member_pointer_v<std::remove_cvref_t<F>>) {
            if (f == nullptr) return;
        }
        if constexpr (kStoredInline<D>) {
            ::new (static_cast<void*>(storage_)) D(std::forward<F>(f));
            table_ = &InlineOps<D>::table;
        } else {
            ::new (static_cast<void*>(storage_)) D*(new D(std::forward<F>(f)));
            table_ = &HeapOps<D>::table;
        }
    }

    UniqueFunction(UniqueFunction&& other) noexcept { take(other); }
    UniqueFunction& operator=(UniqueFunction&& other) noexcept {
        if (this != &other) {
            reset();
            take(other);
        }
        return *this;
    }
    UniqueFunction& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }
    UniqueFunction(const UniqueFunction&) = delete;
    UniqueFunction& operator=(const UniqueFunction&) = delete;
    ~UniqueFunction() { reset(); }

    R operator()(Args... args) { return table_->invoke(storage_, std::forward<Args>(args)...); }

    explicit operator bool() const noexcept { return table_ != nullptr; }

private:
    void take(UniqueFunction& other) noexcept {
        if (other.table_ == nullptr) return;
        other.table_->relocate(storage_, other.storage_);
        table_ = std::exchange(other.table_, nullptr);
    }

    void reset() noexcept {
        if (table_ == nullptr) return;
        table_->destroy(storage_);
        table_ = nullptr;
    }

    alignas(void*) std::byte storage_[kInlineSize];
    const VTable* table_ = nullptr;
};

}  // namespace reboot
