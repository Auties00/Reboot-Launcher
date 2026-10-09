#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "download_support.hpp"
#include "net_test_support.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_client.hpp"
#include "reboot/net/resumable_downloader.hpp"
#include "reboot/testing/fake_disk_info.hpp"
#include "reboot/testing/fake_http_transport.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/in_memory_file_system.hpp"
#include "reboot/testing/scratch_dir.hpp"

using namespace rb;
using namespace rb::net;
using namespace std::chrono_literals;
using rb::net::test::bytes_of;
using rb::net::test::TestStrand;
using rb::testing::FakeHttpResponse;

namespace {

constexpr std::string_view kUrl = "https://builds.test/12.41.zip";
const std::string kBody = "0123456789abcdefghijklmnopqrstuvwxyz";

testing::ScratchDir make_scratch() {
    testing::FakeRandom random(99);
    Result<testing::ScratchDir> dir = testing::ScratchDir::create(random, "reboot-net-dl");
    REQUIRE(dir);
    return std::move(*dir);
}

std::string read_file(const NativePath& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The writer's own view, since progress is coalesced on a clock the test holds still.
std::uintmax_t size_on_disk(const NativePath& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    return error ? 0 : size;
}

void write_file(const NativePath& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

FakeHttpResponse full(std::string etag = "\"v1\"", std::size_t chunk = 8) {
    FakeHttpResponse response;
    response.status = 200;
    response.body = bytes_of(kBody);
    response.headers = {{"ETag", std::move(etag)}};
    response.chunk_size = chunk;
    response.ranges = true;
    return response;
}

struct Fixture {
    Fixture() {
        fs.make_dir(dir.path());
        disk.add_volume(ports::VolumeInfo{.mount = dir.path(), .free_bytes = 1u << 30, .total_bytes = 1u << 31});
    }

    testing::ScratchDir dir = make_scratch();
    NativePath file = dir.path() / "12.41.zip";
    NativePath sidecar = NativePath(file) += ".resume";
    ManualClock clock;
    TestStrand strand{clock};
    TimerService timers{clock, strand};
    testing::FakeHttpTransport transport{strand, clock};
    testing::FakeRandom random{3};
    HostTlsMemory tls{{}, nullptr};
    HttpClient http{transport, tls, strand, timers, random};
    testing::InMemoryFileSystem fs;
    testing::FakeDiskInfo disk;
    WorkerPool workers{1};
    ResumableDownloader downloader{http, disk, fs, workers, strand, timers, clock};

    std::optional<Result<DownloadResult>> result;
    std::vector<DownloadProgress> progress;

    void start(std::optional<u64> expected = std::nullopt, CancelToken token = {}) {
        DownloadRequest request;
        request.url = std::string(kUrl);
        request.file = file;
        request.expected_size = expected;
        REQUIRE(downloader.start(std::move(request), std::move(token),
                                 [this](const DownloadProgress& p) { progress.push_back(p); },
                                 [this](Result<DownloadResult> r) {
                                     REQUIRE_FALSE(result.has_value());
                                     result = std::move(r);
                                 }));
    }

    void finish() { strand.run_until([&] { return result.has_value(); }); }

    // Waits for workers, then lets the downloader's retry timer run.
    void retry_after(std::chrono::milliseconds delay) {
        strand.run_until([&] { return strand.timed_pending() > 0 || result.has_value(); });
        strand.advance(delay);
    }

    std::vector<std::string> ranges() const {
        std::vector<std::string> out;
        for (const ports::HttpRequest& request : transport.requests()) {
            std::string range = "-";
            for (const ports::HttpHeader& header : request.headers)
                if (header.name == "Range") range = header.value;
            out.push_back(range);
        }
        return out;
    }
};

}  // namespace

TEST_CASE("a fresh download writes the file, reports progress and drops the sidecar", "[net][download]") {
    Fixture f;
    f.transport.route("GET", std::string(kUrl), full());
    f.start();
    f.finish();
    REQUIRE(f.result->has_value());
    CHECK((*f.result)->size == kBody.size());
    CHECK_FALSE((*f.result)->resumed);
    CHECK((*f.result)->file == f.file);
    CHECK(read_file(f.file) == kBody);
    CHECK_FALSE(f.fs.exists(f.sidecar));
    REQUIRE_FALSE(f.progress.empty());
    CHECK(f.progress.back().done == kBody.size());
    CHECK(f.progress.back().total == kBody.size());
    CHECK(f.ranges() == std::vector<std::string>{"-"});
}

TEST_CASE("a part file with a sidecar resumes with Range and If-Range", "[net][download]") {
    Fixture f;
    write_file(f.file, kBody.substr(0, 10));
    f.fs.write(f.sidecar, encode_sidecar(ResumeSidecar{std::string(kUrl), "\"v1\"", kBody.size()}));
    f.transport.route("GET", std::string(kUrl), full());
    f.start(kBody.size());
    f.finish();
    REQUIRE(f.result->has_value());
    CHECK((*f.result)->resumed);
    CHECK(read_file(f.file) == kBody);
    REQUIRE(f.transport.requests().size() == 1);
    const ports::HttpRequest sent = f.transport.requests().front();
    CHECK(f.ranges() == std::vector<std::string>{"bytes=10-"});
    bool if_range = false;
    for (const ports::HttpHeader& header : sent.headers) if_range = if_range || (header.name == "If-Range" && header.value == "\"v1\"");
    CHECK(if_range);
}

TEST_CASE("a part file without a sidecar, or for another URL, starts over", "[net][download]") {
    Fixture f;
    write_file(f.file, "stale bytes that must go");
    f.transport.route("GET", std::string(kUrl), full());
    f.start();
    f.finish();
    CHECK(read_file(f.file) == kBody);
    CHECK(f.ranges() == std::vector<std::string>{"-"});

    Fixture other;
    write_file(other.file, kBody.substr(0, 10));
    other.fs.write(other.sidecar, encode_sidecar(ResumeSidecar{"https://elsewhere.test/x.zip", "\"v1\"", kBody.size()}));
    other.transport.route("GET", std::string(kUrl), full());
    other.start();
    other.finish();
    CHECK(read_file(other.file) == kBody);
    CHECK_FALSE((*other.result)->resumed);
}

TEST_CASE("a stalled stream resumes at the written offset", "[net][download]") {
    Fixture f;
    FakeHttpResponse stalls = full();
    stalls.stall_after = 16;
    f.transport.route_sequence("GET", std::string(kUrl), {stalls, full()});
    f.start();
    f.strand.run_until([&] { return size_on_disk(f.file) == 16 && f.strand.timed_pending() > 0; });
    f.strand.advance(30s);
    f.retry_after(1s);
    f.finish();
    REQUIRE(f.result->has_value());
    CHECK((*f.result)->resumed);
    CHECK(read_file(f.file) == kBody);
    CHECK(f.ranges() == std::vector<std::string>{"-", "bytes=16-"});
    CHECK_FALSE(f.fs.exists(f.sidecar));
}

TEST_CASE("a cancelled download keeps its part file and sidecar for later", "[net][download][race]") {
    Fixture f;
    FakeHttpResponse stalls = full();
    stalls.stall_after = 16;
    f.transport.route("GET", std::string(kUrl), stalls);
    CancelSource source;
    f.start(std::nullopt, source.token());
    f.strand.run_until([&] { return size_on_disk(f.file) == 16; });
    source.cancel(CancelReason::User);
    f.finish();
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.download_cancelled");
    CHECK(f.result->error().kind == ErrorKind::Cancelled);
    CHECK(read_file(f.file) == kBody.substr(0, 16));
    const auto sidecar = decode_sidecar(*f.fs.contents(f.sidecar));
    REQUIRE(sidecar);
    CHECK(sidecar->validator == "\"v1\"");
    CHECK(sidecar->total == kBody.size());
}

TEST_CASE("a server ignoring ranges is retried, then the part file is discarded", "[net][download]") {
    Fixture f;
    write_file(f.file, kBody.substr(0, 10));
    f.fs.write(f.sidecar, encode_sidecar(ResumeSidecar{std::string(kUrl), "\"v1\"", kBody.size()}));
    FakeHttpResponse ignores = full();
    ignores.ranges = false;
    f.transport.route("GET", std::string(kUrl), ignores);
    f.start();
    f.retry_after(1s);
    f.retry_after(2s);
    f.retry_after(4s);
    f.finish();
    REQUIRE(f.result->has_value());
    CHECK_FALSE((*f.result)->resumed);
    CHECK(read_file(f.file) == kBody);
    CHECK(f.ranges() == std::vector<std::string>{"bytes=10-", "bytes=10-", "bytes=10-", "-"});
}

TEST_CASE("a changed validator restarts from byte 0 at once", "[net][download]") {
    Fixture f;
    write_file(f.file, std::string(10, 'z'));
    f.fs.write(f.sidecar, encode_sidecar(ResumeSidecar{std::string(kUrl), "\"old\"", kBody.size()}));
    FakeHttpResponse changed = full("\"v2\"");
    changed.ranges = false;
    f.transport.route("GET", std::string(kUrl), changed);
    f.start();
    f.finish();
    REQUIRE(f.result->has_value());
    CHECK(read_file(f.file) == kBody);
    CHECK(f.ranges() == std::vector<std::string>{"bytes=10-", "-"});
}

TEST_CASE("HTTP errors: 404 fails at once, 503 retries until attempts run out", "[net][download]") {
    Fixture missing;
    FakeHttpResponse not_found;
    not_found.status = 404;
    missing.transport.route("GET", std::string(kUrl), not_found);
    missing.start();
    missing.finish();
    REQUIRE_FALSE(missing.result->has_value());
    CHECK(missing.result->error().id == "net.download_http_status");
    CHECK(missing.transport.requests().size() == 1);

    Fixture busy;
    FakeHttpResponse unavailable;
    unavailable.status = 503;
    busy.transport.route("GET", std::string(kUrl), unavailable);
    busy.start();
    for (const auto delay : {1s, 2s, 4s, 8s, 16s, 32s, 60s}) busy.retry_after(delay);
    busy.finish();
    REQUIRE_FALSE(busy.result->has_value());
    CHECK(busy.result->error().id == "net.download_attempts_exhausted");
    REQUIRE(busy.result->error().causes.size() == 1);
    CHECK(busy.result->error().causes.front().id == "net.download_http_status");
    CHECK(busy.transport.requests().size() == 8);
}

TEST_CASE("free space is checked before the first byte or on the first response", "[net][download]") {
    Fixture known;
    known.disk.set_free_bytes(known.dir.path(), 10);
    known.transport.route("GET", std::string(kUrl), full());
    known.start(kBody.size());
    known.finish();
    REQUIRE_FALSE(known.result->has_value());
    CHECK(known.result->error().id == "net.insufficient_space");
    CHECK(known.transport.requests().empty());

    Fixture unknown;
    unknown.disk.set_free_bytes(unknown.dir.path(), 10);
    unknown.transport.route("GET", std::string(kUrl), full());
    unknown.start();
    unknown.finish();
    REQUIRE_FALSE(unknown.result->has_value());
    CHECK(unknown.result->error().id == "net.insufficient_space");
    CHECK(unknown.transport.requests().size() == 1);
}

TEST_CASE("a size that disagrees with the catalog fails without writing", "[net][download]") {
    Fixture f;
    f.transport.route("GET", std::string(kUrl), full());
    f.start(kBody.size() + 1);
    f.finish();
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.download_size_mismatch");
}

TEST_CASE("bad URLs and files outside an existing directory fail synchronously", "[net][download]") {
    Fixture f;
    DownloadRequest bad;
    bad.url = "builds.test/x.zip";
    bad.file = f.file;
    CHECK(f.downloader.start(bad, {}, nullptr, nullptr).error().id == "net.invalid_url");
    DownloadRequest nowhere;
    nowhere.url = std::string(kUrl);
    nowhere.file = f.dir.path() / "missing" / "x.zip";
    CHECK(f.downloader.start(nowhere, {}, nullptr, nullptr).error().id == "net.download_write_failed");
}

TEST_CASE("a sidecar write failure fails the download as a write error", "[net][download]") {
    Fixture f;
    f.fs.faults().fail_next(testing::FsOperation::AtomicReplace, make_diag(ErrorDomain::Platform, MessageId{"platform.io"}).build());
    f.transport.route("GET", std::string(kUrl), full());
    f.start();
    f.finish();
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.download_write_failed");
    REQUIRE(f.result->error().causes.size() == 1);
    CHECK(f.result->error().causes.front().id == "platform.io");
}

namespace {

// Holds the writer at its free-space check until opened, so the queue backs up.
class GatedDisk final : public ports::IDiskInfo {
public:
    explicit GatedDisk(ports::IDiskInfo& inner) : inner_(inner) {}

    Result<std::vector<ports::VolumeInfo>> volumes() override { return inner_.volumes(); }
    Result<ports::VolumeInfo> volume_of(const NativePath& path) override {
        std::unique_lock lock(mutex_);
        opened_.wait_for(lock, std::chrono::seconds{20}, [this] { return open_; });
        return inner_.volume_of(path);
    }

    void open() {
        {
            const std::scoped_lock lock(mutex_);
            open_ = true;
        }
        opened_.notify_all();
    }

private:
    ports::IDiskInfo& inner_;
    std::mutex mutex_;
    std::condition_variable opened_;
    bool open_ = false;
};

}  // namespace

TEST_CASE("a write queue past the backlog aborts, then resumes without spending an attempt", "[net][download]") {
    Fixture f;
    GatedDisk gated(f.disk);
    ResumableDownloader downloader{f.http, gated, f.fs, f.workers, f.strand, f.timers, f.clock};
    constexpr std::size_t kChunk = 8u << 20;
    // Two chunks past the backlog, so it trips whether or not the writer took the first chunk.
    std::vector<u8> body(kDownloadWriteBacklog + 2 * kChunk);
    for (std::size_t i = 0; i < body.size(); ++i) body[i] = static_cast<u8>(i * 31 + i / 4096);
    FakeHttpResponse big;
    big.body = body;
    big.headers = {{"ETag", "\"big\""}};
    big.chunk_size = kChunk;
    big.ranges = true;
    f.transport.route("GET", std::string(kUrl), std::move(big));

    DownloadRequest request;
    request.url = std::string(kUrl);
    request.file = f.file;
    request.retry.max_attempts = 1;
    std::optional<Result<DownloadResult>> result;
    REQUIRE(downloader.start(std::move(request), {}, nullptr, [&](Result<DownloadResult> r) { result = std::move(r); }));
    f.strand.run_until([&] { return !f.transport.requests().empty() && f.transport.in_flight() == 0; });
    gated.open();
    f.strand.run_until([&] { return result.has_value(); });

    REQUIRE(result->has_value());
    CHECK((*result)->size == body.size());
    CHECK((*result)->resumed);
    const std::vector<std::string> ranges = f.ranges();
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0] == "-");
    CHECK((ranges[1] == "bytes=" + std::to_string(kDownloadWriteBacklog) + "-" ||
           ranges[1] == "bytes=" + std::to_string(kDownloadWriteBacklog + kChunk) + "-"));
    std::ifstream in(f.file, std::ios::binary);
    const std::vector<u8> written{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(written == body);
}

TEST_CASE("a part file that vanished before the resume fails instead of gaining a hole", "[net][download]") {
    Fixture f;
    write_file(f.file, kBody.substr(0, 10));
    f.fs.write(f.sidecar, encode_sidecar(ResumeSidecar{std::string(kUrl), "\"v1\"", kBody.size()}));
    FakeHttpResponse later = full();
    later.delay = 1s;
    f.transport.route("GET", std::string(kUrl), later);
    f.start();
    f.strand.run_until([&] { return !f.transport.requests().empty(); });
    std::filesystem::remove(f.file);
    f.strand.advance(1s);
    f.finish();
    REQUIRE_FALSE(f.result->has_value());
    CHECK(f.result->error().id == "net.download_write_failed");
    CHECK_FALSE(std::filesystem::exists(f.file));
}

TEST_CASE("without a stated size the catalog size decides: short resumes, long fails", "[net][download]") {
    Fixture f;
    FakeHttpResponse short_body = full();
    short_body.body = bytes_of(kBody.substr(0, 20));
    // An unparsable length leaves the size unstated.
    short_body.headers.push_back({"Content-Length", ""});
    f.transport.route_sequence("GET", std::string(kUrl), {short_body, full()});
    f.start(kBody.size());
    // The progress timer may be the pending one, so time moves until the retry is sent.
    for (int i = 0; i < 10 && f.transport.requests().size() < 2 && !f.result; ++i) f.retry_after(1s);
    f.finish();
    REQUIRE(f.result->has_value());
    CHECK((*f.result)->size == kBody.size());
    CHECK(read_file(f.file) == kBody);
    CHECK(f.ranges() == std::vector<std::string>{"-", "bytes=20-"});

    Fixture g;
    FakeHttpResponse long_body = full();
    long_body.headers.push_back({"Content-Length", ""});
    g.transport.route("GET", std::string(kUrl), long_body);
    g.start(20);
    g.finish();
    REQUIRE_FALSE(g.result->has_value());
    CHECK(g.result->error().id == "net.download_size_mismatch");
    CHECK(g.transport.requests().size() == 1);
}
