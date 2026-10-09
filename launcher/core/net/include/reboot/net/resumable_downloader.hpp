#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class Executor;
class IClock;
class TimerService;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IDiskInfo;
class IFileSystem;
}  // namespace reboot::ports

namespace reboot::net {

class HttpClient;

// Never NaN: counts are integers and `total` is absent until the server states a size.
struct DownloadProgress {
    u64 done = 0;
    std::optional<u64> total;
    std::optional<u64> bytes_per_s;
};

// Written next to the file as <file>.resume after the first response. A file without one is
// never resumed.
struct ResumeSidecar {
    std::string url;
    // The ETag, or Last-Modified when there is no strong ETag; sent back as If-Range.
    std::string validator;
    std::optional<u64> total;
};

inline constexpr std::string_view kResumeSidecarSuffix = ".resume";
inline constexpr std::size_t kDownloadWriteBacklog = 64u << 20;

struct DownloadRetryPolicy {
    u32 max_attempts = 8;
    // After this many consecutive range failures the part file is discarded and the download
    // starts again from byte 0.
    u32 restart_after_range_failures = 3;
    std::chrono::milliseconds first_delay = std::chrono::seconds{1};
    std::chrono::milliseconds max_delay = std::chrono::seconds{60};
};

struct DownloadRequest {
    std::string url;
    // Inside <dest>/.reboot-staging, so the archive lands on the destination volume.
    NativePath file;
    // From the catalog; lets the free-space check run before the first byte.
    std::optional<u64> expected_size;
    // Connect and stall limits always apply; a total is optional.
    std::optional<std::chrono::milliseconds> total_timeout;
    DownloadRetryPolicy retry;
};

struct DownloadResult {
    NativePath file;
    u64 size = 0;
    bool resumed = false;
};

// Capabilities: matchmaking-networking.http-timeouts.
// One stream per download over HttpClient (HttpDownload limits). At any offset above 0 it sends
// Range plus If-Range and writes only a 206 whose Content-Range starts at that offset; a 200 is
// never written. Free space is checked through IDiskInfo before the first byte when the size is
// known, otherwise on the first response. Strand-only. The transport thread only copies body
// chunks into a write queue that the WorkerPool drains, so a slow disk never stalls other
// transfers. A queue past kDownloadWriteBacklog aborts the stream, and once it drains the download
// resumes at the written offset without spending an attempt. The part file is a plain file stream;
// the sidecar goes through IFileSystem's atomic replace.
class ResumableDownloader {
public:
    ResumableDownloader(HttpClient& http, ports::IDiskInfo& disk, ports::IFileSystem& fs, WorkerPool& workers,
                        Executor& strand, TimerService& timers, IClock& clock);
    ~ResumableDownloader();
    ResumableDownloader(const ResumableDownloader&) = delete;
    ResumableDownloader& operator=(const ResumableDownloader&) = delete;

    // Fails synchronously on a bad URL or a file outside an existing directory. `on_progress` is
    // coalesced to 10 Hz; both callbacks run on the strand, and `done` exactly once. Cancelling
    // keeps the part file and its sidecar for a later resume.
    Result<void> start(DownloadRequest request, CancelToken token,
                       UniqueFunction<void(const DownloadProgress&)> on_progress,
                       UniqueFunction<void(Result<DownloadResult>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::net
