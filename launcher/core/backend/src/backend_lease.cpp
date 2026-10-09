#include "reboot/backend/backend_lease.hpp"

#include <utility>

#include "reboot/backend/backend_service.hpp"

namespace rb::backend {

BackendLease::BackendLease(BackendService& service, u64 id, std::optional<SessionId> session, BackendConfig config)
    : service_(&service), id_(id), session_(session), config_(std::move(config)) {}

BackendLease::BackendLease(BackendLease&& other) noexcept
    : service_(std::exchange(other.service_, nullptr)),
      id_(std::exchange(other.id_, 0)),
      session_(std::exchange(other.session_, std::nullopt)),
      config_(std::move(other.config_)) {}

BackendLease& BackendLease::operator=(BackendLease&& other) noexcept {
    if (this != &other) {
        release();
        service_ = std::exchange(other.service_, nullptr);
        id_ = std::exchange(other.id_, 0);
        session_ = std::exchange(other.session_, std::nullopt);
        config_ = std::move(other.config_);
    }
    return *this;
}

BackendLease::~BackendLease() { release(); }

void BackendLease::release() {
    BackendService* service = std::exchange(service_, nullptr);
    if (service != nullptr) service->release(id_);
}

}  // namespace rb::backend
