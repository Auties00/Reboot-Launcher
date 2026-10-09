#pragma once

#include <memory>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_windows::ipc {

// Covers no capability ids; IIpcListener for the engine. Accepting and every accepted stream run
// on one I/O thread the listener owns; stream callbacks run there, never on the strand.
class NamedPipeListener final : public ports::IIpcListener {
public:
    explicit NamedPipeListener(PipeTrust trust);
    // Closes every stream it still serves, then joins the I/O thread.
    ~NamedPipeListener() override;
    NamedPipeListener(const NamedPipeListener&) = delete;
    NamedPipeListener& operator=(const NamedPipeListener&) = delete;

    // FIRST_PIPE_INSTANCE, so a name someone already holds is platform.pipe_name_taken, not shared.
    // A client reaches `on_accept` only after verify_client; a rejected one is dropped unread.
    Result<void> listen(std::string_view endpoint_name,
                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) override;
    // Stops accepting and closes the waiting instance; accepted streams stay open.
    void close() override;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace rb::os_windows::ipc
