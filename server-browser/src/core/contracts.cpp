// P2900 lets the program supply the contract-violation handler. Providing it here keeps
// statically linked binaries independent of where the toolchain ships its default.
#if SB_HAS_CONTRACTS
#include <contracts>
#include <cstdio>
#include <cstdlib>

void handle_contract_violation(const std::contracts::contract_violation& v) {
    const auto loc = v.location();
    std::fprintf(stderr, "contract violation: %s (%s:%u)\n", v.comment(), loc.file_name(),
                 static_cast<unsigned>(loc.line()));
    std::abort();
}
#endif
