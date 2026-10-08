#include "error_router_impl.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/logging/file_log_sink.hpp"

namespace reboot::logging {

void ErrorRouter::watch(FileLogSink& session_log) {
    if (impl_->unwatch) impl_->unwatch();
    impl_->current_file_name = [&session_log] { return session_log.status().current_file.filename().string(); };
    impl_->unwatch = [&session_log] { session_log.set_on_failure({}); };
    session_log.set_on_failure([link = impl_->link, &strand = impl_->strand](const Diagnostic& failure) {
        strand.post([link, failure] {
            if (link->owner) link->owner->report(failure, LogCategory::Storage, std::nullopt);
        });
    });
}

}  // namespace reboot::logging
