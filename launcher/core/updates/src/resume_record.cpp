#include "reboot/updates/resume_record.hpp"

#include <algorithm>

#include "reboot/storage/resume_document.hpp"

namespace rb::updates {

namespace {

using contracts::ipc::ClientKind;

[[nodiscard]] bool reopenable(ClientKind kind) noexcept {
    return kind == ClientKind::WindowsGui || kind == ClientKind::MacGui || kind == ClientKind::LinuxGui;
}

template <class T>
void append_unique(std::vector<T>& values, const T& value) {
    if (std::ranges::find(values, value) == values.end()) values.push_back(value);
}

}  // namespace

ResumeRecord make_resume_record(contracts::ipc::EngineOrigin origin, const ActivitySnapshot& at_open,
                                const std::optional<ActivitySnapshot>& drained) {
    ResumeRecord record;
    record.origin = origin;
    for (const ClientKind kind : at_open.connected_clients)
        if (reopenable(kind)) append_unique(record.reopen_clients, kind);
    if (!drained) return record;
    for (const LiveActivity& activity : drained->live) {
        if (activity.kind == LiveKind::HostSession && activity.host_profile)
            append_unique(record.relaunch_hosts, *activity.host_profile);
        if (activity.payload_version &&
            (!record.payload_version || *record.payload_version < *activity.payload_version))
            record.payload_version = activity.payload_version;
        if (activity.runtime_id) append_unique(record.runtime_ids, *activity.runtime_id);
    }
    return record;
}

ResumeRecord resume_record_from(const storage::ResumeDocument& document) {
    return ResumeRecord{
        .origin = document.origin,
        .reopen_clients = document.reopen_clients,
        .relaunch_hosts = document.relaunch_hosts,
        .payload_version = document.payload_version,
        .runtime_ids = document.runtime_ids,
    };
}

void store_resume_record(const ResumeRecord& record, storage::ResumeDocument& document) {
    document.origin = record.origin;
    document.reopen_clients = record.reopen_clients;
    document.relaunch_hosts = record.relaunch_hosts;
    document.payload_version = record.payload_version;
    document.runtime_ids = record.runtime_ids;
}

std::vector<std::string> resume_args(contracts::ipc::EngineOrigin origin) {
    std::vector<std::string> args{"run"};
    switch (origin) {
        case contracts::ipc::EngineOrigin::OnDemand: args.emplace_back("--origin=on-demand"); break;
        case contracts::ipc::EngineOrigin::ServiceManager: args.emplace_back("--origin=service-manager"); break;
        case contracts::ipc::EngineOrigin::Foreground: args.emplace_back("--foreground"); break;
    }
    args.emplace_back("--resume");
    return args;
}

}  // namespace rb::updates
