#pragma once

#include <compare>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/secrets/secret_kind.hpp"
#include "reboot/secrets/secret_scope.hpp"

namespace reboot::secrets {

struct SecretTarget {
    SecretKind kind{};
    SecretScope scope;

    auto operator<=>(const SecretTarget&) const = default;

    // Checks scope text from a client against what `kind` expects and canonicalises it;
    // fails with secrets.invalid_scope.
    [[nodiscard]] static Result<SecretTarget> parse(SecretKind kind, std::string_view scope);
};

}  // namespace reboot::secrets
