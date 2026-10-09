#pragma once

#include "reboot/backend/backend_ready.hpp"
#include "reboot/backend/login_observed_event.hpp"
#include "reboot/process/child_exit_info.hpp"

namespace rb::backend {

// BackendService's view of BackendProcess; every call runs on the strand.
class IBackendProcessObserver {
public:
    virtual ~IBackendProcessObserver() = default;

    virtual void on_ready(const BackendReady& ready) = 0;
    // A requested stop arrives with cause Requested, whatever the exit code.
    virtual void on_exit(const process::ChildExitInfo& exit) = 0;
    virtual void on_login_observed(const LoginObservedEvent& event) = 0;
};

}  // namespace rb::backend
