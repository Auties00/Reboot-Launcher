#include "reboot/backend/account_prune_filter.hpp"

namespace reboot::backend {

bool AccountPruneFilter::selects(const BackendAccount& account) const noexcept {
    if (account.kind != BackendAccountKind::Remote || account.record) return false;
    if (!account.last_login || *account.last_login >= last_login_before) return false;
    return !role || *role == account.role;
}

}  // namespace reboot::backend
