#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/testing/fake_quic_peer.hpp"
#include "reboot/testing/fake_quic_transport.hpp"

namespace rb::testing {
namespace {

// What the peer and the client's handle share; posted callbacks hold it too.
struct QuicLink {
    explicit QuicLink(ports::QuicCallbacks client_callbacks) : callbacks(std::move(client_callbacks)) {}

    std::mutex mutex;
    ports::QuicCallbacks callbacks;
    // The client closed or dropped its handle: nothing reaches it any more.
    bool client_gone = false;
    // The server refused or closed the connection.
    bool server_closed = false;
    bool closed_notified = false;

    // Set by the peer, for what the client's handle does to it.
    UniqueFunction<Result<u64>()> open_stream;
    UniqueFunction<Result<void>(u64, std::vector<u8>, bool)> send;
    UniqueFunction<Result<void>(std::vector<u8>)> send_datagram;
    UniqueFunction<void(u64)> close;
    UniqueFunction<void()> release;

    UniqueFunction<void(u64, std::span<const u8>, bool)> stream_hook;
    UniqueFunction<void(std::span<const u8>)> datagram_hook;

    [[nodiscard]] bool reachable() {
        const std::scoped_lock lock(mutex);
        return !client_gone;
    }
};

using Link = std::shared_ptr<QuicLink>;

[[nodiscard]] Diagnostic stream_closed() {
    return make_diag(kTestingDomain, msg::kStreamClosed).kind(ErrorKind::Conflict);
}

class QuicHandle final : public ports::IQuicConnection {
public:
    explicit QuicHandle(Link link) : link_(std::move(link)) {}
    ~QuicHandle() override {
        if (link_->release) link_->release();
    }
    QuicHandle(const QuicHandle&) = delete;
    QuicHandle& operator=(const QuicHandle&) = delete;

    Result<u64> open_stream() override {
        if (!link_->open_stream) return std::unexpected(stream_closed());
        return link_->open_stream();
    }

    Result<void> send(u64 stream_id, std::vector<u8> bytes, bool fin) override {
        if (!link_->send) return std::unexpected(stream_closed());
        return link_->send(stream_id, std::move(bytes), fin);
    }

    Result<void> send_datagram(std::vector<u8> bytes) override {
        if (!link_->send_datagram) return std::unexpected(stream_closed());
        return link_->send_datagram(std::move(bytes));
    }

    void close(u64 app_error) override {
        if (link_->close) link_->close(app_error);
    }

private:
    Link link_;
};

void to_client(Executor& deliver_on, const Link& link, UniqueFunction<void(ports::QuicCallbacks&)> deliver) {
    deliver_on.post([link, deliver = std::move(deliver)]() mutable {
        if (link->reachable()) deliver(link->callbacks);
    });
}

}  // namespace

struct FakeQuicPeer::Stream {
    u64 id = 0;
    std::vector<u8> received;
    bool fin = false;
};

struct FakeQuicPeer::Delivery : QuicLink {
    using QuicLink::QuicLink;
};

FakeQuicPeer::FakeQuicPeer(Executor& deliver_on, ports::QuicConnectOptions options, ports::QuicCallbacks callbacks)
    : deliver_on_(deliver_on), options_(std::move(options)), delivery_(std::make_shared<Delivery>(std::move(callbacks))) {
    QuicLink& link = *delivery_;
    link.open_stream = [this]() -> Result<u64> {
        if (closed_with_ || delivery_->server_closed) return std::unexpected(stream_closed());
        auto stream = std::make_unique<Stream>();
        stream->id = next_stream_id_;
        next_stream_id_ += 4;
        const u64 id = stream->id;
        streams_.push_back(std::move(stream));
        return id;
    };
    link.send = [this](u64 stream_id, std::vector<u8> bytes, bool fin) -> Result<void> {
        if (closed_with_ || delivery_->server_closed) return std::unexpected(stream_closed());
        Stream* target = nullptr;
        for (const auto& stream : streams_)
            if (stream->id == stream_id) target = stream.get();
        if (target == nullptr)
            return make_diag(kTestingDomain, msg::kUnknownStream).arg("stream", stream_id).kind(ErrorKind::InvalidInput).fail();
        if (target->fin) return std::unexpected(stream_closed());
        target->received.insert(target->received.end(), bytes.begin(), bytes.end());
        target->fin = fin;
        if (delivery_->stream_hook) delivery_->stream_hook(stream_id, bytes, fin);
        return {};
    };
    link.send_datagram = [this](std::vector<u8> bytes) -> Result<void> {
        if (closed_with_ || delivery_->server_closed) return std::unexpected(stream_closed());
        datagrams_.push_back(bytes);
        if (delivery_->datagram_hook) delivery_->datagram_hook(bytes);
        return {};
    };
    link.close = [this](u64 app_error) {
        if (closed_with_) return;
        closed_with_ = app_error;
        const std::scoped_lock lock(delivery_->mutex);
        delivery_->client_gone = true;
    };
    link.release = [this] {
        released_ = true;
        const std::scoped_lock lock(delivery_->mutex);
        delivery_->client_gone = true;
    };
}

FakeQuicPeer::~FakeQuicPeer() {
    UniqueFunction<Result<u64>()> open_stream = std::move(delivery_->open_stream);
    UniqueFunction<Result<void>(u64, std::vector<u8>, bool)> send = std::move(delivery_->send);
    UniqueFunction<Result<void>(std::vector<u8>)> send_datagram = std::move(delivery_->send_datagram);
    UniqueFunction<void(u64)> close = std::move(delivery_->close);
    UniqueFunction<void()> release = std::move(delivery_->release);
}

std::unique_ptr<ports::IQuicConnection> FakeQuicPeer::make_handle() { return std::make_unique<QuicHandle>(delivery_); }

void FakeQuicPeer::accept() {
    to_client(deliver_on_, delivery_, [link = Link(delivery_)](ports::QuicCallbacks& callbacks) {
        if (link->server_closed) return;
        if (callbacks.on_connected) callbacks.on_connected();
    });
}

void FakeQuicPeer::refuse(Diagnostic error) { close(std::move(error)); }

void FakeQuicPeer::send(u64 stream_id, std::vector<u8> bytes, bool fin) {
    to_client(deliver_on_, delivery_, [link = Link(delivery_), stream_id, bytes = std::move(bytes), fin](ports::QuicCallbacks& callbacks) {
        if (link->server_closed) return;
        if (callbacks.on_stream_data) callbacks.on_stream_data(stream_id, bytes, fin);
    });
}

void FakeQuicPeer::send_datagram(std::vector<u8> bytes) {
    to_client(deliver_on_, delivery_, [link = Link(delivery_), bytes = std::move(bytes)](ports::QuicCallbacks& callbacks) {
        if (link->server_closed) return;
        if (callbacks.on_datagram) callbacks.on_datagram(bytes);
    });
}

void FakeQuicPeer::close(std::optional<Diagnostic> error) {
    // Data posted before the close still arrives first; nothing sent after it does.
    to_client(deliver_on_, delivery_, [link = Link(delivery_), error = std::move(error)](ports::QuicCallbacks& callbacks) mutable {
        link->server_closed = true;
        if (link->closed_notified) return;
        link->closed_notified = true;
        if (callbacks.on_closed) callbacks.on_closed(std::move(error));
    });
}

void FakeQuicPeer::on_stream_data(UniqueFunction<void(u64 stream_id, std::span<const u8> bytes, bool fin)> handler) {
    delivery_->stream_hook = std::move(handler);
}

void FakeQuicPeer::on_datagram(UniqueFunction<void(std::span<const u8> bytes)> handler) {
    delivery_->datagram_hook = std::move(handler);
}

std::vector<u64> FakeQuicPeer::streams() const {
    std::vector<u64> out;
    out.reserve(streams_.size());
    for (const auto& stream : streams_) out.push_back(stream->id);
    return out;
}

std::vector<u8> FakeQuicPeer::received(u64 stream_id) const {
    for (const auto& stream : streams_)
        if (stream->id == stream_id) return stream->received;
    return {};
}

bool FakeQuicPeer::fin_received(u64 stream_id) const {
    for (const auto& stream : streams_)
        if (stream->id == stream_id) return stream->fin;
    return false;
}

Result<std::unique_ptr<ports::IQuicConnection>> FakeQuicTransport::open_connection(const ports::QuicConnectOptions& options,
                                                                                    ports::QuicCallbacks callbacks) {
    if (next_open_error_) {
        Diagnostic error = std::move(*next_open_error_);
        next_open_error_.reset();
        return std::unexpected(std::move(error));
    }
    auto peer = std::make_unique<FakeQuicPeer>(deliver_on_, options, std::move(callbacks));
    std::unique_ptr<ports::IQuicConnection> handle = peer->make_handle();
    FakeQuicPeer& added = *connections_.emplace_back(std::move(peer));
    if (on_connection_) on_connection_(added);
    return handle;
}

void FakeQuicTransport::on_connection(UniqueFunction<void(FakeQuicPeer&)> handler) { on_connection_ = std::move(handler); }

void FakeQuicTransport::fail_next_open(Diagnostic error) { next_open_error_ = std::move(error); }

std::vector<FakeQuicPeer*> FakeQuicTransport::connections() const {
    std::vector<FakeQuicPeer*> out;
    out.reserve(connections_.size());
    for (const auto& peer : connections_) out.push_back(peer.get());
    return out;
}

FakeQuicPeer* FakeQuicTransport::last() const { return connections_.empty() ? nullptr : connections_.back().get(); }

}  // namespace rb::testing
