#pragma once

#include <chrono>

namespace reboot {

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point steady_now() const = 0;
    [[nodiscard]] virtual std::chrono::system_clock::time_point system_now() const = 0;
};

class SystemClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const override {
        return std::chrono::steady_clock::now();
    }
    [[nodiscard]] std::chrono::system_clock::time_point system_now() const override {
        return std::chrono::system_clock::now();
    }
};

// Moves only when told to; advance() moves both clocks together.
class ManualClock final : public IClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point steady_now() const override { return steady_; }
    [[nodiscard]] std::chrono::system_clock::time_point system_now() const override { return system_; }

    void advance(std::chrono::steady_clock::duration by) {
        steady_ += by;
        system_ += std::chrono::duration_cast<std::chrono::system_clock::duration>(by);
    }
    void set_system(std::chrono::system_clock::time_point at) { system_ = at; }

private:
    std::chrono::steady_clock::time_point steady_{};
    std::chrono::system_clock::time_point system_{};
};

}  // namespace reboot
