#pragma once

#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace rb {

// Volatile stores are never elided, so the wipe survives optimisation.
inline void secure_wipe(void* data, std::size_t size) noexcept {
    auto* p = static_cast<volatile unsigned char*>(data);
    while (size-- > 0) *p++ = 0;
}

template <class T>
class Secret {
public:
    Secret() = default;
    explicit Secret(T value) noexcept : value_(std::move(value)) { wipe(value); }

    Secret(Secret&& other) noexcept : value_(std::move(other.value_)) { wipe(other.value_); }
    Secret& operator=(Secret&& other) noexcept {
        if (this != &other) {
            wipe(value_);
            value_ = std::move(other.value_);
            wipe(other.value_);
        }
        return *this;
    }
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    ~Secret() { wipe(value_); }

    [[nodiscard]] const T& reveal() const noexcept { return value_; }

    friend std::ostream& operator<<(std::ostream& out, const Secret&) { return out << "***"; }

private:
    static void wipe(T& value) noexcept {
        if constexpr (requires { value.capacity(); value.resize(std::size_t{}); value.clear(); }) {
            // Growing to capacity zero-fills the tail, which also clears bytes a short-string
            // move left behind; within capacity this never allocates.
            value.resize(value.capacity());
            secure_wipe(value.data(), value.size() * sizeof(*value.data()));
            value.clear();
        } else {
            secure_wipe(value.data(), value.size() * sizeof(*value.data()));
        }
    }

    T value_{};
};

using SecretString = Secret<std::string>;
using SecretBytes = Secret<std::vector<u8>>;

enum class Sensitivity : u8 { Public, Personal, Secret };

}  // namespace rb

template <class T>
struct std::formatter<rb::Secret<T>, char> : std::formatter<std::string_view, char> {
    auto format(const rb::Secret<T>&, std::format_context& ctx) const {
        return std::formatter<std::string_view, char>::format("***", ctx);
    }
};
