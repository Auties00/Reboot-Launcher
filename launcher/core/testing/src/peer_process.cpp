#include "peer_process.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include "raw_stdio.hpp"
#include "reboot/testing/child_misbehaviour.hpp"
#include "reboot/testing/conformance_report.hpp"

namespace reboot::testing {
namespace {

constexpr std::string_view kControlFlag = "--control=stdio";
constexpr std::string_view kDescribeFlag = "--describe";
constexpr std::string_view kScriptFlag = "--script=";
constexpr std::size_t kReadChunk = 64 * 1024;

// Drains stdout and stderr on its own thread, so the peer never waits on the reader.
class Writer {
public:
    Writer() : thread_([this] { run(); }) {}

    void push(int fd, std::vector<u8> bytes) {
        {
            const std::scoped_lock lock(mutex_);
            queue_.emplace_back(fd, std::move(bytes));
        }
        wake_.notify_one();
    }

    // Writes what is queued, then stops.
    void finish() {
        {
            const std::scoped_lock lock(mutex_);
            done_ = true;
        }
        wake_.notify_one();
        thread_.join();
    }

private:
    void run() {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return done_ || !queue_.empty(); });
            if (queue_.empty()) return;
            auto [fd, bytes] = std::move(queue_.front());
            queue_.pop_front();
            lock.unlock();
            (void)raw_stdio::write_all(fd, bytes.data(), static_cast<unsigned long>(bytes.size()));
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<int, std::vector<u8>>> queue_;
    bool done_ = false;
    std::thread thread_;
};

void usage(const char* program, bool allow_describe) {
    std::fprintf(stderr, "usage: %s %s[--script=<file>]\n", program,
                 allow_describe ? "--describe | --control=stdio " : "--control=stdio ");
}

}  // namespace

std::optional<PeerArguments> parse_peer_arguments(int argc, char** argv, bool allow_describe) {
    const char* program = argc > 0 ? argv[0] : "fake";
    PeerArguments args;
    bool control = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == kControlFlag) {
            control = true;
        } else if (allow_describe && arg == kDescribeFlag) {
            args.describe = true;
        } else if (arg.starts_with(kScriptFlag) && arg.size() > kScriptFlag.size()) {
            args.script = NativePath(std::string(arg.substr(kScriptFlag.size())));
        } else {
            usage(program, allow_describe);
            return std::nullopt;
        }
    }
    if (control == args.describe) {
        usage(program, allow_describe);
        return std::nullopt;
    }
    if (!args.script && argc > 0) {
        // A copy of the fake standing in for the real program brings its script along.
        NativePath beside(argv[0]);
        beside += ".script.json";
        std::error_code error;
        if (std::filesystem::is_regular_file(beside, error)) args.script = std::move(beside);
    }
    return args;
}

void report_bad_script(const Diagnostic& error) { std::fprintf(stderr, "%s\n", describe_diagnostic(error).c_str()); }

void run_peer_process(PeerFactory make) {
    raw_stdio::make_binary();
    Writer writer;
    Strand strand;
    SystemClock clock;
    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    std::thread io_thread([&io] { io.run(); });
    std::atomic<int> exit_code{0};

    std::unique_ptr<IStdioPeer> peer = make(strand, clock, io);
    StdioPeerOutputs outputs;
    outputs.stdout_bytes = [&writer](std::span<const u8> bytes) { writer.push(1, {bytes.begin(), bytes.end()}); };
    outputs.stderr_line = [&writer](std::string_view line) {
        std::vector<u8> bytes(line.begin(), line.end());
        bytes.push_back('\n');
        writer.push(2, std::move(bytes));
    };
    outputs.exit = [&exit_code, &strand](int code) {
        exit_code = code;
        strand.stop();
    };
    strand.post([&peer, outputs = std::move(outputs)]() mutable { peer->start(std::move(outputs)); });

    // Detached: it may sit in a blocking read when the peer exits, and the process ends under it.
    std::thread([&strand, &peer] {
        std::vector<unsigned char> buffer(kReadChunk);
        for (;;) {
            const long got = raw_stdio::read_stdin(buffer.data(), static_cast<unsigned long>(buffer.size()));
            if (got <= 0) {
                strand.post([&peer] { peer->on_stdin_eof(); });
                return;
            }
            strand.post([&peer, bytes = std::vector<u8>(buffer.begin(), buffer.begin() + got)] { peer->on_stdin(bytes); });
        }
    }).detach();

    strand.run();
    writer.finish();
    std::fflush(stderr);
    // Skips destructors: the reader thread may still be blocked on stdin.
    std::_Exit(exit_code.load());
}

}  // namespace reboot::testing
