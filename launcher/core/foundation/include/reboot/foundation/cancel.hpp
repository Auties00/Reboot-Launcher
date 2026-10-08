#pragma once

#include <memory>
#include <optional>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {

enum class CancelReason : u8 { User, Deadline, Shutdown, Disconnect, Superseded };

namespace detail {
struct CancelState;
}

class CancelRegistration {
public:
    CancelRegistration() = default;
    CancelRegistration(CancelRegistration&& other) noexcept;
    CancelRegistration& operator=(CancelRegistration&& other) noexcept;
    CancelRegistration(const CancelRegistration&) = delete;
    CancelRegistration& operator=(const CancelRegistration&) = delete;
    ~CancelRegistration();

    void reset();

private:
    friend class CancelToken;
    CancelRegistration(std::shared_ptr<detail::CancelState> state, u64 id);

    std::shared_ptr<detail::CancelState> state_;
    u64 id_ = 0;
};

// A default-constructed token is never cancelled. Tokens are shared with worker threads,
// so the state is reference counted.
class CancelToken {
public:
    CancelToken() = default;

    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] std::optional<CancelReason> reason() const noexcept;

    // Runs `callback` on the cancelling thread, or immediately if already cancelled.
    [[nodiscard]] CancelRegistration on_cancel(UniqueFunction<void(CancelReason)> callback) const;

private:
    friend class CancelSource;
    explicit CancelToken(std::shared_ptr<detail::CancelState> state);

    std::shared_ptr<detail::CancelState> state_;
};

class CancelSource {
public:
    CancelSource();

    [[nodiscard]] CancelToken token() const;

    // Only the first call wins; its reason is stored by CAS before any callback runs.
    bool cancel(CancelReason reason);
    [[nodiscard]] bool cancelled() const noexcept;

private:
    std::shared_ptr<detail::CancelState> state_;
};

}  // namespace reboot
