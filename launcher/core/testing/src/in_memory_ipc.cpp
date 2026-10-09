#include "reboot/testing/in_memory_ipc.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory_link.hpp"
#include "messages.hpp"

namespace reboot::testing {
namespace {

using Accept = UniqueFunction<void(std::unique_ptr<ports::IByteStream>)>;

struct IpcHub {
    struct Entry {
        ports::PeerIdentity owner;
        // Null for a squatter, which accepts nothing.
        std::shared_ptr<Accept> accept;
        u64 listener = 0;
    };

    IpcHub(Executor& executor, ports::PeerIdentity identity)
        : deliver_on(executor), self(identity), client(std::move(identity)) {}

    mutable std::mutex mutex;
    Executor& deliver_on;
    ports::PeerIdentity self;
    ports::PeerIdentity client;
    std::map<std::string, Entry, std::less<>> endpoints;
    std::vector<std::weak_ptr<MemoryLink>> links;
    u64 next_listener = 1;
};

class MemoryListener final : public ports::IIpcListener {
public:
    explicit MemoryListener(std::shared_ptr<IpcHub> hub) : hub_(std::move(hub)) {}
    ~MemoryListener() override { close(); }
    MemoryListener(const MemoryListener&) = delete;
    MemoryListener& operator=(const MemoryListener&) = delete;

    Result<void> listen(std::string_view endpoint_name,
                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) override {
        const std::scoped_lock lock(hub_->mutex);
        if (hub_->endpoints.contains(endpoint_name) || endpoint_)
            return make_diag(kTestingDomain, msg::kEndpointInUse)
                .arg("endpoint", endpoint_name)
                .kind(ErrorKind::Conflict)
                .fail();
        id_ = hub_->next_listener++;
        hub_->endpoints.emplace(std::string(endpoint_name),
                                IpcHub::Entry{hub_->self, std::make_shared<Accept>(std::move(on_accept)), id_});
        endpoint_ = std::string(endpoint_name);
        return {};
    }

    void close() override {
        std::shared_ptr<Accept> dropped;
        const std::scoped_lock lock(hub_->mutex);
        if (!endpoint_) return;
        if (const auto it = hub_->endpoints.find(*endpoint_); it != hub_->endpoints.end() && it->second.listener == id_) {
            dropped = std::move(it->second.accept);
            hub_->endpoints.erase(it);
        }
        endpoint_.reset();
    }

private:
    std::shared_ptr<IpcHub> hub_;
    std::optional<std::string> endpoint_;
    u64 id_ = 0;
};

class MemoryConnector final : public ports::IIpcConnector {
public:
    explicit MemoryConnector(std::shared_ptr<IpcHub> hub) : hub_(std::move(hub)) {}

    Result<std::unique_ptr<ports::IByteStream>> connect(std::string_view endpoint_name,
                                                        std::chrono::milliseconds) override {
        std::unique_lock lock(hub_->mutex);
        const auto it = hub_->endpoints.find(endpoint_name);
        if (it == hub_->endpoints.end() || (it->second.owner.user_id == hub_->self.user_id && !it->second.accept))
            return make_diag(kTestingDomain, msg::kNoListener)
                .arg("endpoint", endpoint_name)
                .kind(ErrorKind::EngineUnavailable)
                .fail();
        // Checked before anything is written, as the real connectors check the endpoint's owner.
        if (it->second.owner.user_id != hub_->self.user_id) return make_diag(ErrorDomain::Ipc, msg::kEndpointUntrusted).fail();

        const ports::PeerIdentity client = hub_->client;
        LinkedPair pair = make_linked_pair(hub_->deliver_on, it->second.owner, client);
        std::erase_if(hub_->links, [](const std::weak_ptr<MemoryLink>& link) { return link.expired(); });
        hub_->links.push_back(pair.link);
        // The listener drops a client of another user before handing anything over.
        if (client.user_id != it->second.owner.user_id) {
            lock.unlock();
            pair.b.reset();
            return std::move(pair.a);
        }
        hub_->deliver_on.post([hub = hub_, name = std::string(endpoint_name), listener = it->second.listener,
                               accepted = std::move(pair.b)]() mutable {
            std::shared_ptr<Accept> accept;
            {
                const std::scoped_lock deliver_lock(hub->mutex);
                const auto entry = hub->endpoints.find(name);
                if (entry != hub->endpoints.end() && entry->second.listener == listener) accept = entry->second.accept;
            }
            // A listener closed in the meantime drops the stream, so the client sees the close.
            if (accept && *accept) (*accept)(std::move(accepted));
        });
        return std::move(pair.a);
    }

private:
    std::shared_ptr<IpcHub> hub_;
};

}  // namespace

struct InMemoryIpc::Hub : IpcHub {
    using IpcHub::IpcHub;
};

InMemoryIpc::InMemoryIpc(Executor& deliver_on, ports::PeerIdentity self)
    : hub_(std::make_shared<Hub>(deliver_on, std::move(self))) {}

InMemoryIpc::~InMemoryIpc() {
    {
        const std::scoped_lock lock(hub_->mutex);
        hub_->endpoints.clear();
    }
    sever_all();
}

std::unique_ptr<ports::IIpcListener> InMemoryIpc::make_listener() { return std::make_unique<MemoryListener>(hub_); }

std::unique_ptr<ports::IIpcConnector> InMemoryIpc::make_connector() { return std::make_unique<MemoryConnector>(hub_); }

void InMemoryIpc::set_client_identity(ports::PeerIdentity identity) {
    const std::scoped_lock lock(hub_->mutex);
    hub_->client = std::move(identity);
}

void InMemoryIpc::squat(std::string endpoint_name, ports::PeerIdentity owner) {
    const std::scoped_lock lock(hub_->mutex);
    hub_->endpoints.insert_or_assign(std::move(endpoint_name), IpcHub::Entry{std::move(owner), nullptr, 0});
}

void InMemoryIpc::sever_all() {
    std::vector<std::shared_ptr<MemoryLink>> open;
    {
        const std::scoped_lock lock(hub_->mutex);
        for (const std::weak_ptr<MemoryLink>& weak : hub_->links)
            if (auto link = weak.lock()) open.push_back(std::move(link));
        hub_->links.clear();
    }
    for (const auto& link : open) close_link(link);
}

bool InMemoryIpc::listening(std::string_view endpoint_name) const {
    const std::scoped_lock lock(hub_->mutex);
    const auto it = hub_->endpoints.find(endpoint_name);
    return it != hub_->endpoints.end() && it->second.accept != nullptr;
}

std::size_t InMemoryIpc::open_connections() const {
    const std::scoped_lock lock(hub_->mutex);
    return static_cast<std::size_t>(std::ranges::count_if(hub_->links, [](const std::weak_ptr<MemoryLink>& weak) {
        const auto link = weak.lock();
        return link && link_open(*link);
    }));
}

const ports::PeerIdentity& InMemoryIpc::self() const noexcept { return hub_->self; }

}  // namespace reboot::testing
