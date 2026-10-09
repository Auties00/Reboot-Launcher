#include "reboot/testing/fake_backend.hpp"

#include <algorithm>
#include <map>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include "messages.hpp"
#include "peer_process.hpp"
#include "stdio_peer_core.hpp"

namespace rb::testing {
namespace {

namespace asio = boost::asio;
using asio::ip::tcp;
namespace be = contracts::backend;

constexpr std::string_view kInfoPath = "/reboot/v1/backend-info";
// What Ready names when nothing is served: the TCP port no service uses.
constexpr u16 kNothingListens = 1;
constexpr std::chrono::minutes kCredentialLifetime{5};
constexpr u32 kBackendInfoApiVersion = 1;

// GET /reboot/v1/backend-info and a 404 for everything else, one request per connection.
class InfoServer : public std::enable_shared_from_this<InfoServer> {
public:
    explicit InfoServer(asio::io_context& io) : acceptor_(io) {}

    Result<u16> bind(const std::string& address) {
        boost::system::error_code error;
        const auto ip = asio::ip::make_address(address, error);
        if (!error) acceptor_.open(ip.is_v6() ? tcp::v6() : tcp::v4(), error);
        if (!error) acceptor_.bind(tcp::endpoint(ip, 0), error);
        if (!error) acceptor_.listen(asio::socket_base::max_listen_connections, error);
        if (error)
            return make_diag(kTestingDomain, msg::kSocketFailed)
                .arg("operation", "bind")
                .arg("endpoint", address)
                .detail(error.message())
                .os(SystemError{SystemError::Origin::Host, error.value()})
                .fail();
        return acceptor_.local_endpoint().port();
    }

    // The body is fixed before the first accept and only read afterwards.
    void serve(std::string body) {
        body_ = std::move(body);
        asio::post(acceptor_.get_executor(), [self = shared_from_this()] { self->accept(); });
    }

    void stop() {
        asio::post(acceptor_.get_executor(), [self = shared_from_this()] {
            boost::system::error_code ignored;
            self->acceptor_.close(ignored);
        });
    }

private:
    struct Connection {
        explicit Connection(tcp::socket s) : socket(std::move(s)) {}
        tcp::socket socket;
        asio::streambuf request;
        std::string response;
    };

    void accept() {
        acceptor_.async_accept([self = shared_from_this()](const boost::system::error_code& error, tcp::socket socket) {
            if (error) return;
            self->answer(std::make_shared<Connection>(std::move(socket)));
            self->accept();
        });
    }

    void answer(std::shared_ptr<Connection> connection) {
        asio::async_read_until(
            connection->socket, connection->request, "\r\n\r\n",
            [self = shared_from_this(), connection](const boost::system::error_code& error, std::size_t) {
                if (error) return;
                const auto data = connection->request.data();
                const std::string head(asio::buffers_begin(data), asio::buffers_end(data));
                const std::string_view line = std::string_view(head).substr(0, head.find("\r\n"));
                const bool info = line == "GET " + std::string(kInfoPath) + " HTTP/1.1" ||
                                  line == "GET " + std::string(kInfoPath) + " HTTP/1.0";
                const std::string body = info ? self->body_ : std::string("{}");
                connection->response = std::string(info ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found") +
                                       "\r\nContent-Type: application/json\r\nContent-Length: " +
                                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                asio::async_write(connection->socket, asio::buffer(connection->response),
                                  [connection](const boost::system::error_code&, std::size_t) {
                                      boost::system::error_code ignored;
                                      connection->socket.shutdown(tcp::socket::shutdown_both, ignored);
                                      connection->socket.close(ignored);
                                  });
            });
    }

    tcp::acceptor acceptor_;
    std::string body_;
};

[[nodiscard]] u64 unix_ms(std::chrono::system_clock::time_point at) {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count());
}

class BackendCore final : public StdioPeerCore {
public:
    BackendCore(Executor& executor, const IClock& clock, FakeBackendScript script, asio::io_context* http_io)
        : StdioPeerCore(executor, clock, script.child), script_(std::move(script)), http_io_(http_io) {}

    ~BackendCore() override {
        if (server_) server_->stop();
    }

    BackendCore(const BackendCore&) = delete;
    BackendCore& operator=(const BackendCore&) = delete;

    void stop_serving() {
        if (server_) server_->stop();
        server_.reset();
    }

    mutable std::mutex mutex;
    std::optional<be::BackendWelcome> welcome;
    std::vector<be::RegisterAccount> accounts;
    std::vector<be::ConfigureSession> sessions;
    std::size_t minted = 0;
    std::vector<be::MatchTarget> targets;
    // Unix ms of each account's last login; the scripted logins are the only ones.
    std::map<std::string, u64> last_logins;

private:
    void on_finished() override { stop_serving(); }

    void send_hello() override {
        send(be::BackendHello{misbehaviour_.protocol.value_or(be::kBackendProtocol), "fake", script_.content});
    }

    void handle(const OwnedFrame& frame) override {
        bool welcomed = false;
        {
            const std::scoped_lock lock(mutex);
            welcomed = welcome.has_value();
        }
        if (!welcomed) {
            // Nothing but the Welcome means anything before it.
            if (frame.type != contract_frame_type_v<be::BackendWelcome>) return;
            auto message = decode_contract<be::BackendWelcome>(frame.payload);
            if (!message) {
                finish(kBadInvocationExitCode);
                return;
            }
            on_welcome(std::move(*message));
            return;
        }
        dispatch(frame);
    }

    void on_welcome(be::BackendWelcome message) {
        {
            const std::scoped_lock lock(mutex);
            welcome = message;
        }
        reached(ScriptStage::Welcome);
        if (script_.bind_failure) {
            finish(kBindFailureExitCode);
            return;
        }
        after(script_.ready_delay, [this, address = message.bind_address] { become_ready(address); });
    }

    void become_ready(const std::string& address) {
        u16 port = kNothingListens;
        if (script_.serve_http) {
            if (http_io_ == nullptr) {
                finish(kBadInvocationExitCode);
                return;
            }
            server_ = std::make_shared<InfoServer>(*http_io_);
            auto bound = server_->bind(address);
            if (!bound) {
                server_.reset();
                write_stderr("bind failed: " + bound.error().detail.value_or(bound.error().id));
                finish(kBindFailureExitCode);
                return;
            }
            port = *bound;
            // impl and api_version mark a Reboot backend; WebSocket shares the HTTP listener.
            server_->serve(R"({"impl":"reboot","api_version":)" + std::to_string(kBackendInfoApiVersion) +
                           R"(,"version":"fake","ws_port":)" + std::to_string(port) + R"(,"content":{"schema":)" +
                           std::to_string(script_.content.schema) + R"(,"serial":)" +
                           std::to_string(script_.content.serial) + R"(},"http_port":)" + std::to_string(port) + "}");
        }
        http_port_ = port;
        send(be::Ready{port});
        reached(ScriptStage::Ready);
        for (const be::ResolveMatchTarget& request : script_.match_target_requests) send(request);
        for (const be::LoginObserved& login : script_.logins) {
            {
                const std::scoped_lock lock(mutex);
                last_logins.insert_or_assign(login.account_id, unix_ms(clock_.system_now()));
            }
            send(login);
        }
    }

    template <ContractMessage T>
    [[nodiscard]] std::optional<T> decoded(const OwnedFrame& frame) {
        auto message = decode_contract<T>(frame.payload);
        if (message) return std::move(*message);
        const u64 req_id = first_req_id(frame.payload);
        reply([this, req_id, error = std::move(message.error())] { refuse(req_id, error); });
        return std::nullopt;
    }

    void dispatch(const OwnedFrame& frame) {
        const u64 type = frame.type;
        if (type == contract_frame_type_v<be::RegisterAccount>) {
            if (auto request = decoded<be::RegisterAccount>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    std::erase_if(accounts, [&](const be::RegisterAccount& a) { return a.account_id == request->account_id; });
                    accounts.push_back(*request);
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<be::RenameAccount>) {
            if (auto request = decoded<be::RenameAccount>(frame)) rename(*request);
        } else if (type == contract_frame_type_v<be::MintLaunchCredential>) {
            if (auto request = decoded<be::MintLaunchCredential>(frame)) {
                std::size_t serial = 0;
                {
                    const std::scoped_lock lock(mutex);
                    serial = ++minted;
                }
                const auto now = clock_.system_now();
                const std::string prefix = request->kind == be::CredentialKind::ExchangeCode ? "fake-exchange-" : "fake-secret-";
                be::LaunchCredential credential{request->req_id,
                                                prefix + std::to_string(serial) + "-" + std::to_string(unix_ms(now)),
                                                unix_ms(now + kCredentialLifetime)};
                reply([this, credential = std::move(credential)] { send(credential); });
            }
        } else if (type == contract_frame_type_v<be::ConfigureSession>) {
            if (auto request = decoded<be::ConfigureSession>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    std::erase_if(sessions, [&](const be::ConfigureSession& s) { return s.session_key == request->session_key; });
                    sessions.push_back(*request);
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<be::EndSession>) {
            if (auto request = decoded<be::EndSession>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    std::erase_if(sessions, [&](const be::ConfigureSession& s) { return s.session_key == request->session_key; });
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<be::AccountsList>) {
            if (auto request = decoded<be::AccountsList>(frame)) {
                be::AccountsReply answer{request->req_id, {}};
                {
                    const std::scoped_lock lock(mutex);
                    for (const be::RegisterAccount& account : accounts)
                        answer.accounts.push_back({account.account_id, account.record_id, account.role, account.account_id,
                                                   last_login_of(account.account_id)});
                }
                reply([this, answer = std::move(answer)] { send(answer); });
            }
        } else if (type == contract_frame_type_v<be::AccountsReset>) {
            if (auto request = decoded<be::AccountsReset>(frame)) reply([this, id = request->req_id] { ok(id); });
        } else if (type == contract_frame_type_v<be::AccountsDelete>) {
            if (auto request = decoded<be::AccountsDelete>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    std::erase_if(accounts, [&](const be::RegisterAccount& a) { return a.account_id == request->account_id; });
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<be::AccountsPrune>) {
            if (auto request = decoded<be::AccountsPrune>(frame)) {
                // Answered with the accounts it removed.
                be::AccountsReply removed{request->req_id, {}};
                {
                    // An account that never logged in is never pruned.
                    const std::scoped_lock lock(mutex);
                    std::erase_if(accounts, [&](const be::RegisterAccount& a) {
                        const u64 last = last_login_of(a.account_id);
                        const bool pruned =
                            last != 0 && last < request->older_than_unix_ms && (!request->role || a.role == *request->role);
                        if (pruned) removed.accounts.push_back({a.account_id, a.record_id, a.role, a.account_id, last});
                        return pruned;
                    });
                }
                reply([this, removed = std::move(removed)] { send(removed); });
            }
        } else if (type == contract_frame_type_v<be::PurgeData>) {
            if (auto request = decoded<be::PurgeData>(frame)) {
                {
                    const std::scoped_lock lock(mutex);
                    accounts.clear();
                    sessions.clear();
                }
                reply([this, id = request->req_id] { ok(id); });
            }
        } else if (type == contract_frame_type_v<be::ContentInfo>) {
            if (auto request = decoded<be::ContentInfo>(frame))
                reply([this, id = request->req_id] { send(be::ContentInfoReply{id, script_.content}); });
        } else if (type == contract_frame_type_v<be::Health>) {
            if (auto request = decoded<be::Health>(frame))
                reply([this, id = request->req_id] { send(be::HealthReply{id, "fake", script_.content, http_port_}); });
        } else if (type == contract_frame_type_v<be::Drain>) {
            if (auto request = decoded<be::Drain>(frame)) reply([this, id = request->req_id] { ok(id); });
        } else if (type == contract_frame_type_v<be::MatchTarget>) {
            if (auto answer = decoded<be::MatchTarget>(frame)) {
                const std::scoped_lock lock(mutex);
                targets.push_back(std::move(*answer));
            }
        } else {
            const u64 req_id = first_req_id(frame.payload);
            reply([this, req_id] { unsupported(req_id); });
        }
    }

    // Called with `mutex` held; 0 for an account that never logged in.
    [[nodiscard]] u64 last_login_of(const std::string& account_id) const {
        const auto it = last_logins.find(account_id);
        return it == last_logins.end() ? 0 : it->second;
    }

    void rename(const be::RenameAccount& request) {
        std::optional<be::AccountRenameConflict> conflict;
        std::optional<Diagnostic> failure;
        {
            const std::scoped_lock lock(mutex);
            const auto from = std::ranges::find(accounts, request.old_account_id, &be::RegisterAccount::account_id);
            const auto to = std::ranges::find(accounts, request.new_account_id, &be::RegisterAccount::account_id);
            if (from == accounts.end()) {
                failure = make_diag(kTestingDomain, msg::kNotFound)
                              .arg("path", request.old_account_id)
                              .kind(ErrorKind::NotFound)
                              .build();
            } else if (to != accounts.end() && request.on_conflict == be::RenameConflictPolicy::Report) {
                conflict = be::AccountRenameConflict{request.old_account_id, request.new_account_id, to->record_id};
                failure = make_diag(kTestingDomain, msg::kRenameConflict)
                              .arg("account", request.new_account_id)
                              .kind(ErrorKind::Conflict)
                              .build();
            } else if (to != accounts.end() && request.on_conflict == be::RenameConflictPolicy::KeepTarget) {
                accounts.erase(from);
            } else {
                if (to != accounts.end()) accounts.erase(to);
                const auto renamed = std::ranges::find(accounts, request.old_account_id, &be::RegisterAccount::account_id);
                renamed->account_id = request.new_account_id;
                last_logins.erase(request.new_account_id);
                if (auto login = last_logins.extract(request.old_account_id)) {
                    login.key() = request.new_account_id;
                    last_logins.insert(std::move(login));
                }
            }
        }
        reply([this, id = request.req_id, conflict = std::move(conflict), failure = std::move(failure)] {
            if (conflict) send(*conflict);
            if (failure) {
                refuse(id, *failure);
            } else {
                ok(id);
            }
        });
    }

    FakeBackendScript script_;
    asio::io_context* http_io_;
    std::shared_ptr<InfoServer> server_;
    u16 http_port_ = 0;
};

}  // namespace

struct FakeBackend::Impl {
    std::shared_ptr<BackendCore> core;
};

FakeBackend::FakeBackend(Executor& executor, const IClock& clock, FakeBackendScript script)
    : impl_(std::make_unique<Impl>(Impl{std::make_shared<BackendCore>(executor, clock, std::move(script), nullptr)})) {}

FakeBackend::FakeBackend(Executor& executor, const IClock& clock, FakeBackendScript script, boost::asio::io_context& http_io)
    : impl_(std::make_unique<Impl>(Impl{std::make_shared<BackendCore>(executor, clock, std::move(script), &http_io)})) {}

FakeBackend::~FakeBackend() {
    impl_->core->detach();
    impl_->core->stop_serving();
}

void FakeBackend::start(StdioPeerOutputs outputs) { impl_->core->start(std::move(outputs)); }

void FakeBackend::on_stdin(std::span<const u8> bytes) { impl_->core->on_stdin(bytes); }

void FakeBackend::on_stdin_eof() { impl_->core->on_stdin_eof(); }

std::optional<contracts::backend::BackendWelcome> FakeBackend::welcome() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->welcome;
}

std::vector<contracts::backend::RegisterAccount> FakeBackend::registered_accounts() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->accounts;
}

std::vector<contracts::backend::ConfigureSession> FakeBackend::live_sessions() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->sessions;
}

std::size_t FakeBackend::credentials_minted() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->minted;
}

std::vector<contracts::backend::MatchTarget> FakeBackend::match_targets() const {
    const std::scoped_lock lock(impl_->core->mutex);
    return impl_->core->targets;
}

int fake_backend_main(int argc, char** argv) {
    auto args = parse_peer_arguments(argc, argv, false);
    if (!args) return kBadInvocationExitCode;
    FakeBackendScript script;
    if (args->script) {
        auto loaded = load_fake_backend_script(*args->script);
        if (!loaded) {
            report_bad_script(loaded.error());
            return kBadInvocationExitCode;
        }
        script = std::move(*loaded);
    }
    run_peer_process([&script](Executor& executor, const IClock& clock, boost::asio::io_context& io) {
        return std::unique_ptr<IStdioPeer>(std::make_unique<FakeBackend>(executor, clock, std::move(script), io));
    });
}

}  // namespace rb::testing
