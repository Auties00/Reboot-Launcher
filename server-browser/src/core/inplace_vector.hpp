#pragma once

#include <cstddef>
#include <version>

#if __cpp_lib_inplace_vector
#include <inplace_vector>
namespace sb {
template <class T, std::size_t N>
using inplace_vector = std::inplace_vector<T, N>;
}
#elif __has_include(<boost/container/static_vector.hpp>)
#include <boost/container/static_vector.hpp>
namespace sb {
template <class T, std::size_t N>
using inplace_vector = boost::container::static_vector<T, N>;
}
#else
#include <new>
#include <utility>

#include "core/features.hpp"

namespace sb {
// Minimal fixed-capacity vector for toolchains without std::inplace_vector or Boost.
template <class T, std::size_t N>
class inplace_vector {
public:
    inplace_vector() = default;
    inplace_vector(const inplace_vector& o) {
        for (const auto& v : o) push_back(v);
    }
    inplace_vector& operator=(const inplace_vector& o) {
        if (this != &o) {
            clear();
            for (const auto& v : o) push_back(v);
        }
        return *this;
    }
    ~inplace_vector() { clear(); }

    template <class... A>
    T& emplace_back(A&&... a) {
        SB_ASSERT(n_ < N);
        return *new (data() + n_++) T(std::forward<A>(a)...);
    }
    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(std::move(v)); }
    void clear() noexcept {
        while (n_) data()[--n_].~T();
    }
    [[nodiscard]] std::size_t size() const noexcept { return n_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
    [[nodiscard]] bool empty() const noexcept { return n_ == 0; }
    T* data() noexcept { return std::launder(reinterpret_cast<T*>(buf_)); }
    const T* data() const noexcept { return std::launder(reinterpret_cast<const T*>(buf_)); }
    T& operator[](std::size_t i) noexcept { return data()[i]; }
    const T& operator[](std::size_t i) const noexcept { return data()[i]; }
    T* begin() noexcept { return data(); }
    T* end() noexcept { return data() + n_; }
    const T* begin() const noexcept { return data(); }
    const T* end() const noexcept { return data() + n_; }

private:
    alignas(T) unsigned char buf_[sizeof(T) * N];
    std::size_t n_ = 0;
};
}  // namespace sb
#endif
