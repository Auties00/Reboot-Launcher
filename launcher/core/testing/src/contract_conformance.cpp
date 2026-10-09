#include "reboot/testing/contract_conformance.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/json.hpp>

#include "guarded.hpp"
#include "reboot/contracts/backend.hpp"
#include "reboot/contracts/common.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/testing/frame_log.hpp"

namespace reboot::testing {
namespace {

namespace asio = boost::asio;
namespace be = contracts::backend;
namespace gs = contracts::game_server;
namespace common = contracts::common;
using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kQuietWindow{300};
// A frame type in each child's range that no message uses.
constexpr u64 kUnknownBackendFrame = 0x2FE;
constexpr u64 kUnknownServerFrame = 0x3FE;
// TEST-NET-3: never a local address, so binding it must fail.
constexpr std::string_view kUnbindableAddress = "203.0.113.1";

struct ChildLog {
    FrameLog frames{kChildFrameCap};
    std::string err;
    std::optional<ports::ChildExit> exit;
};

// One fresh run of the subject.
struct Run {
    std::unique_ptr<ports::ChildProcess> child;
    Shared<ChildLog> log;

    template <ContractMessage T>
    void send(const T& message) const {
        child->write_stdin(encode_contract_frame(message));
    }

    void send_raw(u64 type, std::vector<u8> payload) const {
        sb::wire::Writer writer;
        writer.quic_varint(type);
        writer.quic_varint(payload.size());
        std::vector<u8> frame = writer.take();
        frame.insert(frame.end(), payload.begin(), payload.end());
        child->write_stdin(frame);
    }

    template <ContractMessage T>
    [[nodiscard]] std::vector<T> all() const {
        auto decoded = log->with([](const ChildLog& l) { return l.frames.all<T>(); });
        return decoded ? std::move(*decoded) : std::vector<T>{};
    }

    [[nodiscard]] bool exited() const { return log->with([](const ChildLog& l) { return l.exit.has_value(); }); }

    ~Run() {
        if (child && !exited()) (void)child->terminate_tree();
    }
    Run() = default;
    Run(Run&&) = default;
    Run& operator=(Run&&) = default;
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
};

[[nodiscard]] Result<Run> spawn(ports::IProcessLauncher& launcher, const ContractSubject& subject, std::string_view mode) {
    ports::ProcessLaunch launch;
    launch.exe = subject.exe;
    launch.args = subject.extra_args;
    launch.args.emplace_back(mode);
    launch.env = subject.env;
    launch.cwd = subject.work_dir;
    launch.stdio = ports::StdioMode::ControlChannel;
    auto child = launcher.spawn(launch);
    if (!child) return std::unexpected(std::move(child.error()));
    Run run;
    run.child = std::move(*child);
    run.log = make_shared_state<ChildLog>();
    run.child->on_stdout([log = run.log](std::span<const u8> bytes) { log->with([&](ChildLog& l) { (void)l.frames.feed(bytes); }); });
    run.child->on_stderr([log = run.log](std::span<const u8> bytes) { log->with([&](ChildLog& l) { l.err.append(bytes.begin(), bytes.end()); }); });
    run.child->on_exit([log = run.log](ports::ChildExit exit) { log->with([&](ChildLog& l) { l.exit = exit; }); });
    return run;
}

// The req_id a reply answers, for every frame type that is one.
[[nodiscard]] std::optional<u64> reply_id(const OwnedFrame& frame) {
    const auto id_of = [&]<class T>(std::type_identity<T>) -> std::optional<u64> {
        if (frame.type != contract_frame_type_v<T>) return std::nullopt;
        auto message = decode_contract<T>(frame.payload);
        return message ? std::optional<u64>(message->req_id) : std::nullopt;
    };
    if (auto id = id_of(std::type_identity<common::CommandResult>{})) return id;
    if (auto id = id_of(std::type_identity<common::Unsupported>{})) return id;
    if (auto id = id_of(std::type_identity<be::HealthReply>{})) return id;
    if (auto id = id_of(std::type_identity<be::ContentInfoReply>{})) return id;
    if (auto id = id_of(std::type_identity<be::AccountsReply>{})) return id;
    if (auto id = id_of(std::type_identity<be::LaunchCredential>{})) return id;
    return std::nullopt;
}

[[nodiscard]] std::size_t replies_to(const Run& run, u64 req_id) {
    return run.log->with([&](const ChildLog& l) {
        return static_cast<std::size_t>(std::ranges::count_if(l.frames.frames(), [&](const OwnedFrame& frame) {
            return reply_id(frame) == req_id;
        }));
    });
}

[[nodiscard]] Uuid conformance_uuid(u8 fill) {
    Uuid uuid;
    uuid.bytes.fill(fill);
    return uuid;
}

// Real time: these talk to real sockets, whatever clock the child runs on.
[[nodiscard]] std::optional<std::string> http_get(u16 port, std::string_view path, std::chrono::milliseconds budget) {
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    std::string request = "GET " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    std::string response;
    std::array<char, 4096> chunk{};
    bool done = false;
    const asio::ip::tcp::endpoint target(asio::ip::address_v4::loopback(), port);
    socket.async_connect(target, [&](const boost::system::error_code& error) {
        if (error) {
            done = true;
            return;
        }
        asio::async_write(socket, asio::buffer(request), [&](const boost::system::error_code& write_error, std::size_t) {
            if (write_error) {
                done = true;
                return;
            }
            const auto read_more = [&](auto& self) -> void {
                socket.async_read_some(asio::buffer(chunk), [&, self](const boost::system::error_code& read_error, std::size_t n) {
                    response.append(chunk.data(), n);
                    if (read_error) {
                        done = true;
                        return;
                    }
                    self(self);
                });
            };
            read_more(read_more);
        });
    });
    io.run_for(budget);
    if (!done || response.empty()) return std::nullopt;
    return response;
}

// The body RemoteBackendProbe recognises: impl "reboot", an api_version and the ws_port.
[[nodiscard]] bool reboot_backend_info(std::string_view response, u16 port) {
    const std::size_t body = response.find("\r\n\r\n");
    if (body == std::string_view::npos) return false;
    boost::system::error_code error;
    const boost::json::value info = boost::json::parse(response.substr(body + 4), error);
    if (error || !info.is_object()) return false;
    const boost::json::object& fields = info.get_object();
    const auto* impl = fields.if_contains("impl");
    const auto* api_version = fields.if_contains("api_version");
    const auto* ws_port = fields.if_contains("ws_port");
    return impl != nullptr && impl->is_string() && impl->get_string() == "reboot" && api_version != nullptr &&
           (api_version->is_int64() || api_version->is_uint64()) && ws_port != nullptr && ws_port->is_int64() &&
           ws_port->get_int64() == port;
}

[[nodiscard]] bool probe_answered(u16 port, std::chrono::milliseconds budget) {
    asio::io_context io;
    asio::ip::udp::socket socket(io);
    boost::system::error_code error;
    socket.open(asio::ip::udp::v4(), error);
    if (error) return false;
    const asio::ip::udp::endpoint target(asio::ip::address_v4::loopback(), port);
    socket.send_to(asio::buffer(gs::kRbsbProbe), target, 0, error);
    if (error) return false;
    std::array<u8, 512> reply{};
    asio::ip::udp::endpoint from;
    bool answered = false;
    socket.async_receive_from(asio::buffer(reply), from, [&](const boost::system::error_code& receive_error, std::size_t) {
        answered = !receive_error;
    });
    io.run_for(budget);
    return answered;
}

[[nodiscard]] std::set<NativePath> tree_of(const NativePath& dir) {
    std::set<NativePath> out;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        out.insert(it->path());
    return out;
}

}  // namespace

ContractConformance::ContractConformance(ports::IProcessLauncher& launcher, IConformanceWaiter& waiter, ContractTiming timing)
    : launcher_(launcher), waiter_(waiter), timing_(timing) {}

ConformanceReport ContractConformance::run_backend(const ContractSubject& subject) {
    ConformanceReport report("backend_contract");
    const NativePath parent = subject.work_dir.parent_path();
    const std::set<NativePath> before = tree_of(parent);

    const auto frames_of = [&](const Run& run, u64 type, std::size_t count, std::chrono::milliseconds budget) {
        return waiter_.wait_until([&] { return run.log->with([&](const ChildLog& l) { return l.frames.count(type) >= count; }); },
                                  budget);
    };
    const auto exits = [&](const Run& run, std::chrono::milliseconds budget) {
        return waiter_.wait_until([&] { return run.exited(); }, budget);
    };
    const auto start = [&](std::string_view address) -> std::optional<Run> {
        auto run = spawn(launcher_, subject, "--control=stdio");
        if (!report.expect_ok("spawn the backend", run)) return std::nullopt;
        if (!frames_of(*run, contract_frame_type_v<be::BackendHello>, 1, timing_.hello)) {
            report.expect("BackendHello arrives within the ChildHello deadline", false);
            return std::nullopt;
        }
        run->send(be::BackendWelcome{std::string(address), to_wire(subject.work_dir), LogLevel::Info});
        return std::move(*run);
    };

    if (auto run = start("127.0.0.1")) {
        const auto hello = run->all<be::BackendHello>();
        report.expect("Hello carries this build's protocol", !hello.empty() && hello.front().protocol == be::kBackendProtocol);
        if (report.expect("Ready follows Welcome", frames_of(*run, contract_frame_type_v<be::Ready>, 1, timing_.ready))) {
            const u16 port = run->all<be::Ready>().front().http_port;
            const auto response = port == 0 ? std::nullopt : http_get(port, "/reboot/v1/backend-info", timing_.reply);
            report.expect("Ready names a serving HTTP listener that answers backend-info",
                          response && response->starts_with("HTTP/1.1 200"));
            report.expect("backend-info names a Reboot backend whose WebSocket shares the HTTP port",
                          response && reboot_backend_info(*response, port));

            run->send(be::Health{101});
            run->send(be::ContentInfo{102});
            run->send(be::RegisterAccount{103, "conformance-account", conformance_uuid(0x11), be::AccountRole::Client});
            run->send(be::ConfigureSession{104, "conformance-key", "conformance-account", "http://127.0.0.1:1/s/conformance-key/",
                                           "console-key", be::GameBuild{"12.41", 1}});
            run->send(be::AccountsList{105});
            run->send(be::EndSession{106, "conformance-key"});
            run->send(be::Drain{107});
            constexpr std::array<u64, 7> kAsked{101, 102, 103, 104, 105, 106, 107};
            const bool answered = waiter_.wait_until(
                [&] { return std::ranges::all_of(kAsked, [&](u64 id) { return replies_to(*run, id) >= 1; }); }, timing_.reply);
            (void)waiter_.wait_until([] { return false; }, kQuietWindow);
            report.expect("every request gets exactly one reply",
                          answered && std::ranges::all_of(kAsked, [&](u64 id) { return replies_to(*run, id) == 1; }));

            run->send_raw(kUnknownBackendFrame, {0x08, 99});
            const bool refused = waiter_.wait_until(
                [&] {
                    const auto unsupported = run->all<common::Unsupported>();
                    return std::ranges::any_of(unsupported, [](const common::Unsupported& u) { return u.req_id == 99; });
                },
                timing_.reply);
            report.expect("an unknown request type gets Unsupported", refused);

            run->send(be::MintLaunchCredential{201, "conformance-account", be::CredentialKind::ExchangeCode, 1});
            run->send(be::MintLaunchCredential{202, "conformance-account", be::CredentialKind::LaunchSecret, 1});
            if (report.expect("both credentials arrive",
                              frames_of(*run, contract_frame_type_v<be::LaunchCredential>, 2, timing_.reply))) {
                const auto credentials = run->all<be::LaunchCredential>();
                report.expect("credentials are unique and not empty",
                              credentials.size() >= 2 && !credentials[0].value.empty() &&
                                  credentials[0].value != credentials[1].value);
                report.expect("credentials expire", std::ranges::all_of(credentials, [](const be::LaunchCredential& c) {
                                  return c.expires_at_unix_ms > 0;
                              }));
            }
        }
        run->child->close_stdin();
        report.expect("stdin EOF ends the backend within the grace", exits(*run, timing_.stop_grace));
    }

    if (auto run = start(kUnbindableAddress)) {
        const bool ended = exits(*run, timing_.ready);
        const auto exit = run->log->copy().exit;
        report.expect("a bind failure exits non-zero", ended && exit && exit->code.value_or(0) != 0);
        report.expect("a bind failure sends no Ready", run->all<be::Ready>().empty());
    }

    if (parent.empty() || !std::filesystem::is_directory(parent)) {
        report.skip("writes stay inside the work dir", "the work dir has no parent to watch");
    } else {
        std::vector<std::string> strays;
        for (const NativePath& path : tree_of(parent))
            if (!before.contains(path) && !is_inside(path, subject.work_dir)) strays.push_back(display_utf8(path));
        report.expect("writes stay inside the work dir", strays.empty(), strays.empty() ? std::string() : strays.front());
    }
    return report;
}

ConformanceReport ContractConformance::run_game_server(const ContractSubject& subject,
                                                       const GameServerConformanceOptions& options) {
    ConformanceReport report("game_server_contract");
    const auto frames_of = [&](const Run& run, u64 type, std::size_t count, std::chrono::milliseconds budget) {
        return waiter_.wait_until([&] { return run.log->with([&](const ChildLog& l) { return l.frames.count(type) >= count; }); },
                                  budget);
    };
    const auto exits = [&](const Run& run, std::chrono::milliseconds budget) {
        return waiter_.wait_until([&] { return run.exited(); }, budget);
    };

    std::optional<gs::GameServerDescription> description;
    if (auto run = spawn(launcher_, subject, "--describe"); report.expect_ok("spawn --describe", run)) {
        const bool ended = exits(*run, timing_.hello);
        const ChildLog log = run->log->copy();
        report.expect("--describe exits 0", ended && log.exit && log.exit->code == 0);
        const auto described = log.frames.all<gs::GameServerDescription>();
        report.expect("--describe writes exactly one description frame",
                      described && described->size() == 1 && log.frames.frames().size() == 1);
        if (described && !described->empty()) description = described->front();
    }
    if (!description) return report;
    report.expect("the description carries this build's protocol", description->protocol == gs::kGameServerProtocol);
    if (!report.expect("the description declares sockets", !description->sockets.empty())) return report;
    const std::size_t socket_count = description->sockets.size();

    const auto config_for = [&](std::vector<u16> ports) {
        gs::ServerConfig config;
        config.session_id = conformance_uuid(0x22);
        config.game = options.game;
        config.listen = gs::ListenConfig{"127.0.0.1", std::move(ports)};
        config.match = gs::MatchConfig{"playlist_defaultsolo", gs::StartPolicy::Manual, 0, 100, 30};
        config.log_dir = to_wire(subject.work_dir);
        return config;
    };
    std::vector<u16> block;
    for (std::size_t i = 0; i < socket_count; ++i) block.push_back(static_cast<u16>(options.first_port.value + i));
    const u16 occupied_port = static_cast<u16>(options.first_port.value + socket_count);

    // Spawns, checks Hello against --describe, sends Welcome and waits for Listening or ListenFailed.
    const auto start = [&](std::vector<u16> ports, bool check_hello) -> std::optional<Run> {
        auto run = spawn(launcher_, subject, "--control=stdio");
        if (!report.expect_ok("spawn the game server", run)) return std::nullopt;
        if (!frames_of(*run, contract_frame_type_v<gs::ServerHello>, 1, timing_.hello)) {
            report.expect("ServerHello arrives within the ChildHello deadline", false);
            return std::nullopt;
        }
        if (check_hello) {
            const auto hello = run->all<gs::ServerHello>();
            report.expect("ServerHello repeats the description",
                          !hello.empty() && encode_contract_frame(hello.front().description) == encode_contract_frame(*description));
        }
        run->send(gs::ServerWelcome{config_for(std::move(ports))});
        (void)waiter_.wait_until(
            [&] {
                return run->log->with([](const ChildLog& l) {
                    return l.frames.count(contract_frame_type_v<gs::Listening>) > 0 ||
                           l.frames.count(contract_frame_type_v<gs::ListenFailed>) > 0;
                });
            },
            timing_.ready);
        return std::move(*run);
    };

    if (auto run = start(block, true)) {
        const auto listening = run->all<gs::Listening>();
        bool exact = !listening.empty() && listening.front().bound.size() == socket_count;
        for (std::size_t i = 0; exact && i < socket_count; ++i)
            exact = listening.front().bound[i].port == block[i] && listening.front().bound[i].role == description->sockets[i].role;
        report.expect("Listening reports exactly the Welcome ports", exact);

        std::optional<u16> game_port;
        for (std::size_t i = 0; i < socket_count; ++i)
            if (description->sockets[i].role == gs::SocketRole::Game) game_port = block[i];
        if (game_port) {
            report.expect("the game port answers the rbsb probe", probe_answered(*game_port, timing_.reply));
        } else {
            report.skip("the game port answers the rbsb probe", "no Game socket declared");
        }

        run->send(gs::Reset{301});
        const bool reset = waiter_.wait_until([&] { return replies_to(*run, 301) == 1; }, timing_.reply);
        const auto results = run->all<common::CommandResult>();
        report.expect("Reset succeeds", reset && std::ranges::any_of(results, [](const common::CommandResult& r) {
                                            return r.req_id == 301 && r.ok;
                                        }));
        if (game_port) report.expect("Reset keeps the block", probe_answered(*game_port, timing_.reply));

        run->send(gs::StartMatch{302, 0});
        run->send(gs::EndMatch{303});
        run->send(gs::Kick{304, 1, "conformance"});
        run->send(gs::SetBans{305, {gs::Ban{"198.51.100.7", std::nullopt, std::nullopt, "conformance"}}});
        run->send(gs::SetOperators{306, {"127.0.0.1/32"}});
        run->send(gs::RunCommand{307, "status"});
        run->send(gs::Drain{308});
        constexpr std::array<u64, 7> kAsked{302, 303, 304, 305, 306, 307, 308};
        const bool answered = waiter_.wait_until(
            [&] { return std::ranges::all_of(kAsked, [&](u64 id) { return replies_to(*run, id) >= 1; }); }, timing_.reply);
        (void)waiter_.wait_until([] { return false; }, kQuietWindow);
        report.expect("every request gets exactly one reply",
                      answered && std::ranges::all_of(kAsked, [&](u64 id) { return replies_to(*run, id) == 1; }));

        run->send_raw(kUnknownServerFrame, {0x08, 99});
        report.expect("an unknown request type gets Unsupported", waiter_.wait_until([&] { return replies_to(*run, 99) == 1; },
                                                                                       timing_.reply));

        const auto grace = std::chrono::duration_cast<std::chrono::milliseconds>(timing_.stop_grace);
        run->send(gs::Shutdown{309, static_cast<u32>(grace.count())});
        report.expect("Shutdown is answered", waiter_.wait_until([&] { return replies_to(*run, 309) == 1; }, timing_.reply));
        report.expect("Shutdown ends the server within the grace", exits(*run, timing_.stop_grace));
    }

    if (auto run = start(block, false)) {
        run->child->close_stdin();
        report.expect("stdin EOF ends the server within the grace", exits(*run, timing_.stop_grace));
    }

    {
        asio::io_context io;
        asio::ip::udp::socket holder(io);
        boost::system::error_code error;
        holder.open(asio::ip::udp::v4(), error);
        if (!error) holder.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), occupied_port), error);
        if (!report.expect("occupy a port", !error, error.message())) return report;
        std::vector<u16> ports = block;
        ports.front() = occupied_port;
        if (auto run = start(ports, false)) {
            const auto failed = run->all<gs::ListenFailed>();
            report.expect("an occupied port gives ListenFailed for it",
                          !failed.empty() && failed.front().port == occupied_port && run->all<gs::Listening>().empty());
            const bool ended = exits(*run, timing_.stop_grace);
            const auto exit = run->log->copy().exit;
            report.expect("a bind failure exits non-zero", ended && exit && exit->code.value_or(0) != 0);
        }
    }
    return report;
}

}  // namespace reboot::testing
