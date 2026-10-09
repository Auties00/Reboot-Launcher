#include "reboot/process/orphan_reaper.hpp"

#include "messages.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/ports/process.hpp"

namespace rb::process {

namespace {

[[nodiscard]] OrphanReapReport reap_all(ports::IProcessLauncher& launcher, const std::vector<ChildRecord>& recorded,
                                        const CancelToken& token) {
    OrphanReapReport report;
    for (const ChildRecord& record : recorded) {
        if (token.cancelled()) {
            report.failed.emplace_back(record, make_diag(ErrorDomain::Process, msg::kReapCancelled)
                                                   .arg("pid", record.pid)
                                                   .kind(ErrorKind::Cancelled)
                                                   .build());
            continue;
        }
        Result<bool> alive = launcher.is_alive(record.pid, record.created);
        if (alive && !*alive) {
            report.gone.push_back(record);
            continue;
        }
        Result<void> killed = alive ? launcher.kill(record.pid, record.created) : std::unexpected(std::move(alive.error()));
        if (killed) {
            report.killed.push_back(record);
            continue;
        }
        report.failed.emplace_back(record, make_diag(ErrorDomain::Process, msg::kReapFailed)
                                               .arg("pid", record.pid)
                                               .cause(std::move(killed.error()))
                                               .build());
    }
    return report;
}

}  // namespace

void OrphanReaper::reap(std::vector<ChildRecord> recorded, CancelToken token,
                        UniqueFunction<void(Result<OrphanReapReport>)> done) {
    ports::IProcessLauncher& launcher = launcher_;
    workers_.submit<OrphanReapReport>(
        [&launcher, recorded = std::move(recorded)](CancelToken work_token) -> Result<OrphanReapReport> {
            return reap_all(launcher, recorded, work_token);
        },
        std::move(token), strand_, std::move(done));
}

}  // namespace rb::process
