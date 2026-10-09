#include "tcp_byte_stream.hpp"

#include <array>
#include <deque>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/secret.hpp"

namespace reboot::game_channel {

namespace {

namespace asio = boost::asio;
using asio::ip::tcp;

constexpr std::size_t kReadChunk = 64 * 1024;

void wipe(std::vector<u8>& bytes) noexcept { secure_wipe(bytes.data(), bytes.size()); }

// Socket work runs on the socket's executor; the callbacks and the inbox are under `mutex`, since
// the owner installs callbacks from the strand.
struct TcpState : std::enable_shared_from_this<TcpState> {
    explicit TcpState(tcp::socket accepted) : socket(std::move(accepted)) {}

    tcp::socket socket;
    std::array<u8, kReadChunk> buffer{};
    std::deque<std::vector<u8>> outbox;
    bool writing = false;
    bool closing = false;

    std::mutex mutex;
    UniqueFunction<void(std::span<const u8>)> on_read;
    UniqueFunction<void()> on_close;
    std::deque<std::vector<u8>> inbox;
    bool in_callback = false;
    bool released = false;
    bool closed = false;
    bool close_delivered = false;

    // Queued bytes first, then the close once nothing is left to read.
    void deliver() {
        std::unique_lock lock(mutex);
        while (!released && !inbox.empty() && on_read && !in_callback) {
            std::vector<u8> bytes = std::move(inbox.front());
            inbox.pop_front();
            UniqueFunction<void(std::span<const u8>)> callback = std::move(on_read);
            in_callback = true;
            lock.unlock();
            callback(bytes);
            wipe(bytes);
            lock.lock();
            in_callback = false;
            if (!on_read && !released) on_read = std::move(callback);
        }
        if (released || !closed || close_delivered || !inbox.empty() || in_callback || !on_close) return;
        close_delivered = true;
        UniqueFunction<void()> callback = std::move(on_close);
        lock.unlock();
        callback();
    }

    void read() {
        socket.async_read_some(asio::buffer(buffer), [self = shared_from_this()](const boost::system::error_code& error,
                                                                                std::size_t n) {
            if (error) {
                self->shut();
                return;
            }
            {
                const std::scoped_lock lock(self->mutex);
                // Nobody reads after the owner is gone; queued writes may still keep the socket open.
                if (!self->released)
                    self->inbox.emplace_back(self->buffer.begin(), self->buffer.begin() + static_cast<std::ptrdiff_t>(n));
            }
            secure_wipe(self->buffer.data(), n);
            self->deliver();
            self->read();
        });
    }

    void write_next() {
        if (writing || !socket.is_open()) return;
        if (outbox.empty()) {
            if (closing) shut();
            return;
        }
        writing = true;
        asio::async_write(socket, asio::buffer(outbox.front()),
                          [self = shared_from_this()](const boost::system::error_code& error, std::size_t) {
                              self->writing = false;
                              wipe(self->outbox.front());
                              self->outbox.pop_front();
                              if (error) {
                                  self->shut();
                                  return;
                              }
                              self->write_next();
                          });
    }

    void shut() {
        boost::system::error_code ignored;
        if (socket.is_open()) {
            socket.shutdown(tcp::socket::shutdown_both, ignored);
            socket.close(ignored);
        }
        // A write in flight still owns the front buffer until its handler runs.
        for (std::size_t i = writing ? 1 : 0; i < outbox.size(); ++i) wipe(outbox[i]);
        outbox.resize(writing ? 1 : 0);
        {
            const std::scoped_lock lock(mutex);
            closed = true;
        }
        deliver();
    }
};

class TcpByteStream final : public ports::IByteStream {
public:
    explicit TcpByteStream(std::shared_ptr<TcpState> state) : state_(std::move(state)) {
        asio::post(state_->socket.get_executor(), [state = state_] { state->read(); });
    }

    ~TcpByteStream() override {
        UniqueFunction<void(std::span<const u8>)> on_read;
        UniqueFunction<void()> on_close;
        {
            const std::scoped_lock lock(state_->mutex);
            state_->released = true;
            on_read = std::move(state_->on_read);
            on_close = std::move(state_->on_close);
            for (std::vector<u8>& bytes : state_->inbox) wipe(bytes);
            state_->inbox.clear();
        }
        close();
    }

    TcpByteStream(const TcpByteStream&) = delete;
    TcpByteStream& operator=(const TcpByteStream&) = delete;

    void write(std::span<const u8> bytes) override {
        if (bytes.empty()) return;
        asio::post(state_->socket.get_executor(), [state = state_, data = std::vector<u8>(bytes.begin(), bytes.end())]() mutable {
            if (!state->socket.is_open() || state->closing) {
                wipe(data);
                return;
            }
            state->outbox.push_back(std::move(data));
            state->write_next();
        });
    }

    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override {
        {
            const std::scoped_lock lock(state_->mutex);
            state_->on_read = std::move(callback);
        }
        asio::post(state_->socket.get_executor(), [state = state_] { state->deliver(); });
    }

    void on_close(UniqueFunction<void()> callback) override {
        {
            const std::scoped_lock lock(state_->mutex);
            state_->on_close = std::move(callback);
        }
        asio::post(state_->socket.get_executor(), [state = state_] { state->deliver(); });
    }

    void close() override {
        asio::post(state_->socket.get_executor(), [state = state_] {
            state->closing = true;
            state->write_next();
        });
    }

    [[nodiscard]] ports::PeerIdentity peer() const override { return {}; }

private:
    std::shared_ptr<TcpState> state_;
};

}  // namespace

std::unique_ptr<ports::IByteStream> make_tcp_byte_stream(tcp::socket socket) {
    boost::system::error_code ignored;
    socket.set_option(tcp::no_delay(true), ignored);
    return std::make_unique<TcpByteStream>(std::make_shared<TcpState>(std::move(socket)));
}

}  // namespace reboot::game_channel
