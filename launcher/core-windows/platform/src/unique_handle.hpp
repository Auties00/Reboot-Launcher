#pragma once

#include <utility>

#include "win32.hpp"

namespace rb::os_windows::platform {

// Owns a HANDLE; both null and INVALID_HANDLE_VALUE mean none.
class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE release() noexcept { return std::exchange(handle_, nullptr); }
    void reset(HANDLE handle = nullptr) noexcept {
        if (*this) CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

// Releases a COM interface pointer.
template <class T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    ~ComPtr() { reset(); }
    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }
    // For out-parameters; releases what was held first.
    [[nodiscard]] T** put() noexcept {
        reset();
        return &ptr_;
    }
    [[nodiscard]] void** put_void() noexcept { return reinterpret_cast<void**>(put()); }
    void reset() noexcept {
        if (ptr_ != nullptr) ptr_->Release();
        ptr_ = nullptr;
    }

private:
    T* ptr_ = nullptr;
};

// Frees a BSTR.
class Bstr {
public:
    Bstr() noexcept = default;
    explicit Bstr(const wchar_t* text) : value_(SysAllocString(text)) {}
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    [[nodiscard]] BSTR get() const noexcept { return value_; }
    [[nodiscard]] BSTR* put() noexcept {
        SysFreeString(value_);
        value_ = nullptr;
        return &value_;
    }

private:
    BSTR value_ = nullptr;
};

}  // namespace rb::os_windows::platform
