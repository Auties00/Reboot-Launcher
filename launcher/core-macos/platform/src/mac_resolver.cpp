#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_resolver.hpp"

#include <dispatch/dispatch.h>
#include <dns_sd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

#include "dns_answers.hpp"
#include "messages.hpp"
#include "reboot/foundation/log.hpp"

// DISPATCH_QUEUE_SERIAL is a C-style cast in Apple's headers.
#pragma clang diagnostic ignored "-Wold-style-cast"

namespace reboot::os_macos::platform {

namespace {

using Done = UniqueFunction<void(Result<std::vector<IpAddress>>)>;

[[nodiscard]] Diagnostic cancelled(const std::string& host) {
    return make_diag(ErrorDomain::Platform, kDnsCancelled).arg("host", host).kind(ErrorKind::Cancelled).build();
}

[[nodiscard]] Diagnostic dns_failed(const std::string& host, DNSServiceErrorType error) {
    const bool missing = error == kDNSServiceErr_NoSuchName || error == kDNSServiceErr_NoSuchRecord;
    return make_diag(ErrorDomain::Platform, kDnsFailed)
        .arg("host", host)
        .os(SystemError{SystemError::Origin::Host, error})
        .kind(missing ? ErrorKind::NotFound : ErrorKind::Generic)
        .retryable(!missing)
        .build();
}

// One lookup. Everything but the cancel registration runs on the resolver's serial queue; the
// query keeps itself alive until `done` has run there.
class Query : public std::enable_shared_from_this<Query> {
public:
    Query(std::string host, CancelToken token, Done done, dispatch_queue_t queue)
        : host_(std::move(host)), token_(std::move(token)), done_(std::move(done)), queue_(queue) {
        ::dispatch_retain(queue_);
    }
    ~Query() { ::dispatch_release(queue_); }
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    // On the queue.
    void start() {
        if (token_.cancelled()) {
            finish(std::unexpected(cancelled(host_)));
            return;
        }
        if (std::optional<IpAddress> literal = IpAddress::parse(host_)) {
            finish(std::vector<IpAddress>{*literal});
            return;
        }
        switch (special_name(host_)) {
            case SpecialName::Loopback:
                finish(std::vector<IpAddress>{IpAddress::v4(0x7F000001), *IpAddress::parse("::1")});
                return;
            case SpecialName::Invalid:
                finish(std::unexpected(dns_failed(host_, kDNSServiceErr_NoSuchName)));
                return;
            case SpecialName::None:
                break;
        }
        const DNSServiceErrorType started = ::DNSServiceGetAddrInfo(
            &ref_, kDNSServiceFlagsReturnIntermediates | kDNSServiceFlagsTimeout, 0,
            kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6, host_.c_str(), &Query::on_answer, this);
        if (started != kDNSServiceErr_NoError) {
            ref_ = nullptr;
            finish(std::unexpected(dns_failed(host_, started)));
            return;
        }
        if (const DNSServiceErrorType queued = ::DNSServiceSetDispatchQueue(ref_, queue_);
            queued != kDNSServiceErr_NoError) {
            finish(std::unexpected(dns_failed(host_, queued)));
            return;
        }
        self_ = shared_from_this();
        std::weak_ptr<Query> weak = self_;
        CancelRegistration registration = token_.on_cancel([weak](CancelReason) {
            if (std::shared_ptr<Query> live = weak.lock()) live->post(&Query::cancel_on_queue);
        });
        if (!finished_) registration_ = std::move(registration);
    }

    // Hands the queue a strong reference to this query and runs `step` there.
    void post(void (*step)(void*)) {
        ::dispatch_async_f(queue_, new std::shared_ptr<Query>(shared_from_this()), step);
    }

    static void start_on_queue(void* context) { run(context, [](Query& query) { query.start(); }); }

private:
    template <class Step>
    static void run(void* context, Step step) {
        const std::unique_ptr<std::shared_ptr<Query>> query{static_cast<std::shared_ptr<Query>*>(context)};
        try {
            step(**query);
        } catch (...) {
            REBOOT_LOG_ERROR(Net, "internal.bug: a DNS lookup threw");
        }
    }

    static void cancel_on_queue(void* context) {
        run(context, [](Query& query) { query.finish(std::unexpected(cancelled(query.host_))); });
    }

    static void on_answer(DNSServiceRef, DNSServiceFlags flags, uint32_t, DNSServiceErrorType error, const char*,
                          const sockaddr* address, uint32_t, void* context) {
        auto& query = *static_cast<Query*>(context);
        try {
            query.answer(flags, error, address);
        } catch (...) {
            REBOOT_LOG_ERROR(Net, "internal.bug: a DNS answer threw");
        }
    }

    void answer(DNSServiceFlags flags, DNSServiceErrorType error, const sockaddr* address) {
        if (finished_) return;
        if (error == kDNSServiceErr_NoSuchRecord && address != nullptr) {
            answers_.none(address->sa_family == AF_INET6 ? DnsFamily::V6 : DnsFamily::V4);
        } else if (error != kDNSServiceErr_NoError) {
            if (answers_.addresses().empty()) {
                finish(std::unexpected(dns_failed(host_, error)));
            } else {
                finish(answers_.addresses());
            }
            return;
        } else if ((flags & kDNSServiceFlagsAdd) != 0 && address != nullptr) {
            IpAddress ip;
            if (address->sa_family == AF_INET) {
                sockaddr_in v4{};
                std::memcpy(&v4, address, sizeof v4);
                std::memcpy(ip.bytes.data() + 12, &v4.sin_addr, 4);
                ip.bytes[10] = 0xFF;
                ip.bytes[11] = 0xFF;
                answers_.add(ip);
            } else if (address->sa_family == AF_INET6) {
                sockaddr_in6 v6{};
                std::memcpy(&v6, address, sizeof v6);
                std::memcpy(ip.bytes.data(), &v6.sin6_addr, ip.bytes.size());
                answers_.add(ip);
            }
        }
        if ((flags & kDNSServiceFlagsMoreComing) != 0 || !answers_.complete()) return;
        if (answers_.addresses().empty()) {
            finish(std::unexpected(dns_failed(host_, kDNSServiceErr_NoSuchRecord)));
        } else {
            finish(answers_.addresses());
        }
    }

    // On the queue, exactly once.
    void finish(Result<std::vector<IpAddress>> outcome) {
        if (finished_) return;
        finished_ = true;
        if (ref_ != nullptr) {
            ::DNSServiceRefDeallocate(ref_);
            ref_ = nullptr;
        }
        Done done = std::move(done_);
        // A cancel callback running elsewhere only posts, so resetting here never waits long.
        registration_.reset();
        const std::shared_ptr<Query> keep = std::move(self_);
        try {
            done(std::move(outcome));
        } catch (...) {
            REBOOT_LOG_ERROR(Net, "internal.bug: a resolver callback threw");
        }
    }

    std::string host_;
    CancelToken token_;
    Done done_;
    dispatch_queue_t queue_;
    DNSServiceRef ref_ = nullptr;
    DnsAnswers answers_;
    bool finished_ = false;
    std::shared_ptr<Query> self_;
    CancelRegistration registration_;
};

}  // namespace

struct MacResolver::Impl {
    dispatch_queue_t queue = ::dispatch_queue_create("dev.projectreboot.launcher.dns", DISPATCH_QUEUE_SERIAL);

    Impl() = default;
    // Lookups in flight retain the queue themselves.
    ~Impl() { ::dispatch_release(queue); }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

MacResolver::MacResolver() : impl_(std::make_unique<Impl>()) {}

MacResolver::~MacResolver() = default;

void MacResolver::resolve(std::string host, CancelToken token, UniqueFunction<void(Result<std::vector<IpAddress>>)> done) {
    auto query = std::make_shared<Query>(std::move(host), std::move(token), std::move(done), impl_->queue);
    query->post(&Query::start_on_queue);
}

}  // namespace reboot::os_macos::platform
