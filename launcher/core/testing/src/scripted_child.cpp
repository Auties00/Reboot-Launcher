#include "reboot/testing/scripted_child.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rb::testing {
namespace {

struct StdoutChunk {
    std::vector<u8> bytes;
};
struct StderrChunk {
    std::vector<u8> bytes;
};
using ChildEvent = std::variant<StdoutChunk, StderrChunk, ports::ChildExit>;

// What the child and the parent's handle share; posted deliveries hold it too.
struct ChildChannel {
    explicit ChildChannel(Executor& executor) : io(executor) {}

    std::mutex mutex;
    Executor& io;
    ScriptedChild* child = nullptr;
    // Set by the child, for what the parent's handle does to it.
    UniqueFunction<void(std::span<const u8>)> write_stdin;
    UniqueFunction<void()> close_stdin;
    UniqueFunction<void()> terminate;
    UniqueFunction<void()> release;
    UniqueFunction<void(std::vector<u8>)> peer_stdin;
    UniqueFunction<void()> peer_eof;
    UniqueFunction<void()> drop_peer;

    UniqueFunction<void(std::span<const u8>)> on_stdout;
    UniqueFunction<void(std::span<const u8>)> on_stderr;
    UniqueFunction<void(ports::ChildExit)> on_exit;
    std::deque<ChildEvent> pending;
    bool handle_alive = true;
};

using Channel = std::shared_ptr<ChildChannel>;

// In order; unheard output is dropped, but the exit waits for on_exit.
void deliver(const Channel& channel) {
    std::unique_lock lock(channel->mutex);
    while (channel->handle_alive && !channel->pending.empty()) {
        ChildEvent& event = channel->pending.front();
        if (auto* exit = std::get_if<ports::ChildExit>(&event)) {
            if (!channel->on_exit) return;
            const ports::ChildExit code = *exit;
            channel->pending.pop_front();
            UniqueFunction<void(ports::ChildExit)> callback = std::move(channel->on_exit);
            lock.unlock();
            callback(code);
            return;
        }
        const bool is_stdout = std::holds_alternative<StdoutChunk>(event);
        std::vector<u8> bytes = is_stdout ? std::move(std::get<StdoutChunk>(event).bytes)
                                          : std::move(std::get<StderrChunk>(event).bytes);
        channel->pending.pop_front();
        auto& slot = is_stdout ? channel->on_stdout : channel->on_stderr;
        if (!slot) continue;
        UniqueFunction<void(std::span<const u8>)> callback = std::move(slot);
        lock.unlock();
        callback(bytes);
        lock.lock();
        auto& again = is_stdout ? channel->on_stdout : channel->on_stderr;
        if (!again && channel->handle_alive) again = std::move(callback);
    }
}

void schedule(const Channel& channel) {
    channel->io.post([channel] { deliver(channel); });
}

class ChildHandle final : public ports::ChildProcess {
public:
    ChildHandle(Channel channel, u32 pid, std::chrono::system_clock::time_point created)
        : channel_(std::move(channel)), pid_(pid), created_(created) {}

    ~ChildHandle() override {
        UniqueFunction<void(std::span<const u8>)> on_stdout;
        UniqueFunction<void(std::span<const u8>)> on_stderr;
        UniqueFunction<void(ports::ChildExit)> on_exit;
        {
            const std::scoped_lock lock(channel_->mutex);
            channel_->handle_alive = false;
            on_stdout = std::move(channel_->on_stdout);
            on_stderr = std::move(channel_->on_stderr);
            on_exit = std::move(channel_->on_exit);
        }
        if (channel_->release) channel_->release();
    }

    ChildHandle(const ChildHandle&) = delete;
    ChildHandle& operator=(const ChildHandle&) = delete;

    [[nodiscard]] u32 pid() const override { return pid_; }
    [[nodiscard]] std::chrono::system_clock::time_point created() const override { return created_; }

    void write_stdin(std::span<const u8> bytes) override {
        if (channel_->write_stdin) channel_->write_stdin(bytes);
    }

    void close_stdin() override {
        if (channel_->close_stdin) channel_->close_stdin();
    }

    void on_stdout(UniqueFunction<void(std::span<const u8>)> callback) override {
        set(channel_->on_stdout, std::move(callback));
    }

    void on_stderr(UniqueFunction<void(std::span<const u8>)> callback) override {
        set(channel_->on_stderr, std::move(callback));
    }

    void on_exit(UniqueFunction<void(ports::ChildExit)> callback) override { set(channel_->on_exit, std::move(callback)); }

    Result<void> terminate_tree() override {
        if (channel_->terminate) channel_->terminate();
        return {};
    }

private:
    template <class F>
    void set(F& slot, F callback) {
        {
            const std::scoped_lock lock(channel_->mutex);
            slot = std::move(callback);
        }
        schedule(channel_);
    }

    Channel channel_;
    u32 pid_;
    std::chrono::system_clock::time_point created_;
};

}  // namespace

struct ScriptedChild::Callbacks : ChildChannel {
    using ChildChannel::ChildChannel;
};

ScriptedChild::ScriptedChild(Executor& io, u32 pid, std::chrono::system_clock::time_point created,
                             ports::ProcessLaunch launch, ports::ChildExit terminated_exit)
    : io_(io),
      pid_(pid),
      created_(created),
      launch_(std::move(launch)),
      terminated_exit_(terminated_exit),
      callbacks_(std::make_shared<Callbacks>(io)) {
    ChildChannel& channel = *callbacks_;
    channel.child = this;
    channel.write_stdin = [this](std::span<const u8> bytes) {
        if (exit_ || stdin_closed_) return;
        (void)stdin_frames_.feed(bytes);
        if (!peer_) return;
        io_.post([channel = Channel(callbacks_), data = std::vector<u8>(bytes.begin(), bytes.end())]() mutable {
            if (channel->peer_stdin) channel->peer_stdin(std::move(data));
        });
    };
    channel.close_stdin = [this] {
        if (exit_ || stdin_closed_) return;
        stdin_closed_ = true;
        if (!peer_) return;
        io_.post([channel = Channel(callbacks_)] {
            if (channel->peer_eof) channel->peer_eof();
        });
    };
    channel.terminate = [this] {
        if (exit_) return;
        terminated_ = true;
        exit(terminated_exit_);
    };
    channel.release = [this] { released_ = true; };
    channel.peer_stdin = [this](std::vector<u8> bytes) {
        if (peer_ && !exit_) peer_->on_stdin(bytes);
    };
    channel.peer_eof = [this] {
        if (peer_ && !exit_) peer_->on_stdin_eof();
    };
    channel.drop_peer = [this] { peer_.reset(); };
}

ScriptedChild::~ScriptedChild() {
    peer_.reset();
    ChildChannel& channel = *callbacks_;
    UniqueFunction<void(std::span<const u8>)> write_stdin;
    UniqueFunction<void()> close_stdin;
    UniqueFunction<void()> terminate;
    UniqueFunction<void()> release;
    UniqueFunction<void(std::vector<u8>)> peer_stdin;
    UniqueFunction<void()> peer_eof;
    UniqueFunction<void()> drop_peer;
    const std::scoped_lock lock(channel.mutex);
    channel.child = nullptr;
    write_stdin = std::move(channel.write_stdin);
    close_stdin = std::move(channel.close_stdin);
    terminate = std::move(channel.terminate);
    release = std::move(channel.release);
    peer_stdin = std::move(channel.peer_stdin);
    peer_eof = std::move(channel.peer_eof);
    drop_peer = std::move(channel.drop_peer);
}

std::unique_ptr<ports::ChildProcess> ScriptedChild::make_handle() {
    return std::make_unique<ChildHandle>(callbacks_, pid_, created_);
}

void ScriptedChild::write_stdout(std::span<const u8> bytes) {
    if (exit_ || bytes.empty()) return;
    {
        const std::scoped_lock lock(callbacks_->mutex);
        callbacks_->pending.emplace_back(StdoutChunk{{bytes.begin(), bytes.end()}});
    }
    schedule(callbacks_);
}

void ScriptedChild::write_stderr_line(std::string_view line) {
    if (exit_) return;
    std::vector<u8> bytes(line.begin(), line.end());
    bytes.push_back('\n');
    {
        const std::scoped_lock lock(callbacks_->mutex);
        callbacks_->pending.emplace_back(StderrChunk{std::move(bytes)});
    }
    schedule(callbacks_);
}

void ScriptedChild::exit(ports::ChildExit exit) {
    if (exit_) return;
    exit_ = exit;
    {
        const std::scoped_lock lock(callbacks_->mutex);
        callbacks_->pending.emplace_back(exit);
    }
    schedule(callbacks_);
    // Later, since the peer may be the caller; a killed peer releases what it holds, such as ports.
    if (peer_)
        io_.post([channel = Channel(callbacks_)] {
            if (channel->drop_peer) channel->drop_peer();
        });
}

void ScriptedChild::attach_peer(std::unique_ptr<IStdioPeer> peer) {
    peer_ = std::move(peer);
    StdioPeerOutputs outputs;
    const std::weak_ptr<ChildChannel> weak = Channel(callbacks_);
    outputs.stdout_bytes = [weak](std::span<const u8> bytes) {
        if (const auto channel = weak.lock(); channel && channel->child != nullptr) channel->child->write_stdout(bytes);
    };
    outputs.stderr_line = [weak](std::string_view line) {
        if (const auto channel = weak.lock(); channel && channel->child != nullptr) channel->child->write_stderr_line(line);
    };
    outputs.exit = [weak](int code) {
        if (const auto channel = weak.lock(); channel && channel->child != nullptr)
            channel->child->exit(ports::ChildExit{code, std::nullopt});
    };
    peer_->start(std::move(outputs));
}

}  // namespace rb::testing
