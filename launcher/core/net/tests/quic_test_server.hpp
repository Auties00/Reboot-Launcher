#pragma once

#include <memory>
#include <string>

#include "reboot/foundation/types.hpp"
#include "test_pki.hpp"

namespace rb::net::test {

// An MsQuic listener on 127.0.0.1 with the test PKI's localhost certificate and ALPN rbsb/1. Each
// stream a client opens is echoed back, ending with FIN, once the client ends it. With `push`, the
// server opens one unidirectional stream per connection that carries `push` and ends.
class QuicTestServer {
public:
    explicit QuicTestServer(const TestPki& pki, std::string push = {});
    // Shuts every connection down and waits for MsQuic to finish with them.
    ~QuicTestServer();
    QuicTestServer(const QuicTestServer&) = delete;
    QuicTestServer& operator=(const QuicTestServer&) = delete;

    [[nodiscard]] u16 port() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net::test
