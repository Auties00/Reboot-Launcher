#include "reboot/backend/backend_config.hpp"

#include <utility>

namespace rb::backend {

Result<BackendConfig> BackendConfig::from_settings(const storage::BackendSettings& settings) {
    return BackendTarget::from_settings(settings.target).transform([&](BackendTarget target) {
        return BackendConfig{std::move(target), settings.allow_lan};
    });
}

}  // namespace rb::backend
