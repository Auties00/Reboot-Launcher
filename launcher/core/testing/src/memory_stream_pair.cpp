#include "reboot/testing/memory_stream_pair.hpp"

#include <array>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "memory_link.hpp"

namespace rb::testing {

struct MemoryLink {
    struct End {
        UniqueFunction<void(std::span<const u8>)> on_read;
        UniqueFunction<void()> on_close;
        std::deque<std::vector<u8>> inbox;
        // Set while a callback runs, so one installed from inside it is kept.
        bool reading = false;
        bool alive = true;
        bool close_pending = false;
        bool close_delivered = false;
    };

    explicit MemoryLink(Executor& executor) : deliver_on(executor) {}

    mutable std::mutex mutex;
    Executor& deliver_on;
    std::array<End, 2> ends;
    bool closed = false;
};

namespace {

void pump(const std::shared_ptr<MemoryLink>& link, std::size_t side);

void schedule_pump(const std::shared_ptr<MemoryLink>& link, std::size_t side) {
    link->deliver_on.post([link, side] { pump(link, side); });
}

// Delivers queued bytes, then the close once nothing is left to read.
void pump(const std::shared_ptr<MemoryLink>& link, std::size_t side) {
    std::unique_lock lock(link->mutex);
    MemoryLink::End& end = link->ends[side];
    while (end.alive && !end.inbox.empty() && end.on_read && !end.reading) {
        std::vector<u8> bytes = std::move(end.inbox.front());
        end.inbox.pop_front();
        UniqueFunction<void(std::span<const u8>)> callback = std::move(end.on_read);
        end.reading = true;
        lock.unlock();
        callback(bytes);
        lock.lock();
        end.reading = false;
        if (!end.on_read && end.alive) end.on_read = std::move(callback);
    }
    if (!end.alive || end.reading || !end.close_pending || end.close_delivered || !end.inbox.empty() || !end.on_close)
        return;
    end.close_delivered = true;
    UniqueFunction<void()> callback = std::move(end.on_close);
    lock.unlock();
    callback();
}

void close_locked(const std::shared_ptr<MemoryLink>& link, std::unique_lock<std::mutex>& lock) {
    if (link->closed) return;
    link->closed = true;
    for (MemoryLink::End& end : link->ends) end.close_pending = true;
    lock.unlock();
    schedule_pump(link, 0);
    schedule_pump(link, 1);
}

class MemoryStream final : public ports::IByteStream {
public:
    MemoryStream(std::shared_ptr<MemoryLink> link, std::size_t side, ports::PeerIdentity peer)
        : link_(std::move(link)), side_(side), peer_(std::move(peer)) {}

    ~MemoryStream() override {
        UniqueFunction<void(std::span<const u8>)> on_read;
        UniqueFunction<void()> on_close;
        std::unique_lock lock(link_->mutex);
        MemoryLink::End& end = link_->ends[side_];
        end.alive = false;
        on_read = std::move(end.on_read);
        on_close = std::move(end.on_close);
        close_locked(link_, lock);
    }

    MemoryStream(const MemoryStream&) = delete;
    MemoryStream& operator=(const MemoryStream&) = delete;

    void write(std::span<const u8> bytes) override {
        if (bytes.empty()) return;
        const std::size_t other = 1 - side_;
        {
            const std::scoped_lock lock(link_->mutex);
            if (link_->closed || !link_->ends[other].alive) return;
            link_->ends[other].inbox.emplace_back(bytes.begin(), bytes.end());
        }
        schedule_pump(link_, other);
    }

    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override {
        {
            const std::scoped_lock lock(link_->mutex);
            link_->ends[side_].on_read = std::move(callback);
        }
        schedule_pump(link_, side_);
    }

    void on_close(UniqueFunction<void()> callback) override {
        {
            const std::scoped_lock lock(link_->mutex);
            link_->ends[side_].on_close = std::move(callback);
        }
        schedule_pump(link_, side_);
    }

    void close() override {
        std::unique_lock lock(link_->mutex);
        close_locked(link_, lock);
    }

    [[nodiscard]] ports::PeerIdentity peer() const override { return peer_; }

private:
    std::shared_ptr<MemoryLink> link_;
    std::size_t side_;
    ports::PeerIdentity peer_;
};

}  // namespace

LinkedPair make_linked_pair(Executor& deliver_on, ports::PeerIdentity a_peer, ports::PeerIdentity b_peer) {
    auto link = std::make_shared<MemoryLink>(deliver_on);
    LinkedPair pair;
    pair.a = std::make_unique<MemoryStream>(link, 0, std::move(a_peer));
    pair.b = std::make_unique<MemoryStream>(link, 1, std::move(b_peer));
    pair.link = std::move(link);
    return pair;
}

void close_link(const std::shared_ptr<MemoryLink>& link) {
    std::unique_lock lock(link->mutex);
    close_locked(link, lock);
}

bool link_open(const MemoryLink& link) {
    const std::scoped_lock lock(link.mutex);
    return !link.closed;
}

MemoryStreamPair make_memory_stream_pair(Executor& deliver_on, ports::PeerIdentity a_peer, ports::PeerIdentity b_peer) {
    LinkedPair pair = make_linked_pair(deliver_on, std::move(a_peer), std::move(b_peer));
    return {std::move(pair.a), std::move(pair.b)};
}

}  // namespace rb::testing
