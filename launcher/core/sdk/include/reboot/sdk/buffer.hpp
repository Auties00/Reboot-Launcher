#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "reboot/client.h"

namespace rb::sdk {

// Owns one rb_buffer; reboot_client wipes it when it is released.
class Buffer {
public:
    Buffer() noexcept = default;
    ~Buffer() { release(); }
    Buffer(Buffer&& other) noexcept : raw_(std::exchange(other.raw_, rb_buffer{})) {}
    Buffer& operator=(Buffer&& other) noexcept {
        if (this != &other) {
            release();
            raw_ = std::exchange(other.raw_, rb_buffer{});
        }
        return *this;
    }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // For a C call's output argument; whatever the buffer held is released first.
    [[nodiscard]] rb_buffer* out() noexcept {
        release();
        return &raw_;
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return raw_.data == nullptr ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>(raw_.data, raw_.size);
    }
    [[nodiscard]] bool empty() const noexcept { return raw_.size == 0; }

    void release() noexcept { rb_buffer_release(&raw_); }

private:
    rb_buffer raw_{};
};

}  // namespace rb::sdk
