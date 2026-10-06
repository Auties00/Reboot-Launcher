#pragma once

#include <string>

namespace sb::security {

struct PemFiles {
    std::string cert;
    std::string key;
};

// Development only: writes a fresh ECDSA P-256 certificate for "localhost" into `dir`.
[[nodiscard]] PemFiles write_self_signed(const std::string& dir);

}  // namespace sb::security
