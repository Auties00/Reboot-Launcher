#include "tcp_stream.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>

#include "messages.hpp"

namespace rb::testing {
namespace {

namespace asio = boost::asio;
using asio::ip::tcp;

constexpr std::size_t kReadChunk = 64 * 1024;

[[nodiscard]] asio::ip::address to_asio(const IpAddress& address) {
    if (address.is_v4())
        return asio::ip::address_v4(static_cast<asio::ip::address_v4::uint_type>(
            u32{address.bytes[12]} << 24 | u32{address.bytes[13]} << 16 | u32{address.bytes[14]} << 8 | address.bytes[15]));
    asio::ip::address_v6::bytes_type bytes{};
    std::copy(address.bytes.begin(), address.bytes.end(), bytes.begin());
    return asio::ip::address_v6(bytes);
}

struct TcpState : std::enable_shared_from_this<TcpState> {
    explicit TcpState(asio::io_context& io) : strand(asio::make_strand(io)), socket(strand) {}

    asio::strand<asio::io_context::executor_type> strand;
    tcp::socket socket;
    std::array<u8, kReadChunk> buffer{};
    std::deque<std::vector<u8>> outbox;
    bool writing = false;
    // close() waits for what was written before it.
    bool closing = false;

    std::mutex mutex;
    UniqueFunction<void(std::span<const u8>)> on_read;
    UniqueFunction<void()> on_close;
    std::deque<std::vector<u8>> inbox;
    bool reading_callback = false;
    // The TcpStream is gone: no callback runs or is restored any more.
    bool released = false;
    bool closed = false;
    bool close_delivered = false;

    // Runs on the strand: queued bytes first, then the close once nothing is left to read.
    void deliver() {
        std::unique_lock lock(mutex);
        while (!released && !inbox.empty() && on_read && !reading_callback) {
            std::vector<u8> bytes = std::move(inbox.front());
            inbox.pop_front();
            UniqueFunction<void(std::span<const u8>)> callback = std::move(on_read);
            reading_callback = true;
            lock.unlock();
            callback(bytes);
            lock.lock();
            reading_callback = false;
            if (!on_read && !released) on_read = std::move(callback);
        }
        if (released || !closed || close_delivered || !inbox.empty() || reading_callback || !on_close) return;
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
                self->inbox.emplace_back(self->buffer.begin(), self->buffer.begin() + static_cast<std::ptrdiff_t>(n));
            }
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
        {
            const std::scoped_lock lock(mutex);
            closed = true;
        }
        deliver();
    }
};

class TcpStream final : public ports::IByteStream {
public:
    explicit TcpStream(std::shared_ptr<TcpState> state) : state_(std::move(state)) {}

    ~TcpStream() override {
        UniqueFunction<void(std::span<const u8>)> on_read;
        UniqueFunction<void()> on_close;
        {
            const std::scoped_lock lock(state_->mutex);
            state_->released = true;
            on_read = std::move(state_->on_read);
            on_close = std::move(state_->on_close);
        }
        close();
    }

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;

    void write(std::span<const u8> bytes) override {
        asio::post(state_->strand, [state = state_, data = std::vector<u8>(bytes.begin(), bytes.end())]() mutable {
            if (!state->socket.is_open() || state->closing) return;
            state->outbox.push_back(std::move(data));
            state->write_next();
        });
    }

    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override {
        {
            const std::scoped_lock lock(state_->mutex);
            state_->on_read = std::move(callback);
        }
        asio::post(state_->strand, [state = state_] { state->deliver(); });
    }

    void on_close(UniqueFunction<void()> callback) override {
        {
            const std::scoped_lock lock(state_->mutex);
            state_->on_close = std::move(callback);
        }
        asio::post(state_->strand, [state = state_] { state->deliver(); });
    }

    void close() override {
        asio::post(state_->strand, [state = state_] {
            state->closing = true;
            state->write_next();
        });
    }

    [[nodiscard]] ports::PeerIdentity peer() const override { return {}; }

private:
    std::shared_ptr<TcpState> state_;
};

}  // namespace

Result<std::unique_ptr<ports::IByteStream>> connect_tcp(boost::asio::io_context& io, Endpoint endpoint) {
    auto state = std::make_shared<TcpState>(io);
    boost::system::error_code error;
    state->socket.connect(tcp::endpoint(to_asio(endpoint.address), endpoint.port.value), error);
    if (error)
        return make_diag(kTestingDomain, msg::kSocketFailed)
            .arg("operation", "connect")
            .arg("endpoint", endpoint.to_string())
            .detail(error.message())
            .os(SystemError{SystemError::Origin::Host, error.value()})
            .fail();
    asio::post(state->strand, [state] { state->read(); });
    return std::unique_ptr<ports::IByteStream>(std::make_unique<TcpStream>(state));
}

}  // namespace rb::testing
