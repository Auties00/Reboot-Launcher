#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// IResolver over answers the test sets, posted to `deliver_on`; IP literals and "localhost" resolve
// themselves, unknown and .invalid hosts fail NotFound, and `done` always runs exactly once.
class FakeResolver final : public ports::IResolver {
public:
    explicit FakeResolver(Executor& deliver_on);
    ~FakeResolver() override;
    FakeResolver(const FakeResolver&) = delete;
    FakeResolver& operator=(const FakeResolver&) = delete;

    void resolve(std::string host, CancelToken token, UniqueFunction<void(Result<std::vector<IpAddress>>)> done) override;

    void set(std::string host, std::vector<IpAddress> addresses);
    void fail(std::string host, Diagnostic error);
    // Answers only once its token is cancelled, with testing.cancelled, for the Dns deadline path.
    void hang(std::string host);

    // Every host asked for, in order.
    [[nodiscard]] std::vector<std::string> queries() const;
    [[nodiscard]] std::size_t pending() const;

private:
    struct State;
    // Shared with cancel registrations, which can fire after the resolver is gone.
    std::shared_ptr<State> state_;
};

}  // namespace reboot::testing
