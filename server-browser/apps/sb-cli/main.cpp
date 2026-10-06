// sb-cli: browse, search, resolve, join and host against an edge.

#include <CLI/CLI.hpp>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <thread>

#include "client/client.hpp"
#include "client/view_mirror.hpp"
#include "core/time.hpp"
#include "ops/admin_http.hpp"
#include "registry/validation.hpp"
#include "security/crypto.hpp"

using namespace sb;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_stop{false};

class Inbox {
public:
    void push(client::Event e) {
        {
            std::lock_guard lk(mu_);
            q_.push_back(std::move(e));
        }
        cv_.notify_one();
    }
    std::optional<client::Event> pop(std::chrono::milliseconds timeout) {
        std::unique_lock lk(mu_);
        if (!cv_.wait_for(lk, timeout, [&] { return !q_.empty(); })) return std::nullopt;
        auto e = std::move(q_.front());
        q_.pop_front();
        return e;
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<client::Event> q_;
};

std::string hex(std::span<const u8> b) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (u8 x : b) {
        s.push_back(d[x >> 4]);
        s.push_back(d[x & 0xF]);
    }
    return s;
}

std::string addr_string(const wire::Bytes& a) {
    if (a.size() == 4) return std::to_string(a[0]) + "." + std::to_string(a[1]) + "." + std::to_string(a[2]) + "." + std::to_string(a[3]);
    IpAddr ip;
    if (a.size() == 16) std::copy(a.begin(), a.end(), ip.bytes.begin());
    return "[" + ip.to_string() + "]";
}

wire::Sort parse_sort(const std::string& s) {
    if (s == "newest") return wire::Sort::newest;
    if (s == "name") return wire::Sort::name;
    return wire::Sort::players;
}

wire::PasswordFilter parse_pwd(const std::string& s) {
    if (s == "none") return wire::PasswordFilter::none;
    if (s == "only") return wire::PasswordFilter::only;
    return wire::PasswordFilter::any;
}

void print_entry(const wire::ListEntry& e) {
    std::printf("%-36s %-28.28s %-12.12s %-6.6s %4u/%-4u %s%s\n", e.id.to_string().c_str(), e.name.c_str(),
                e.author.c_str(), e.version.c_str(), e.players, e.max_players,
                (e.flags & wire::entry_flag::has_password) ? "locked " : "",
                (e.flags & wire::entry_flag::reachable) ? "" : "unreachable");
}

// Waits for the first event of type T (or an Error), printing nothing else.
template <class T>
std::optional<T> await(Inbox& in, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline && !g_stop) {
        auto e = in.pop(100ms);
        if (!e) continue;
        if (auto* v = std::get_if<T>(&*e)) return *v;
        if (auto* err = std::get_if<wire::Error>(&*e)) {
            std::fprintf(stderr, "error %u: %s\n", static_cast<unsigned>(err->code), err->message.c_str());
            return std::nullopt;
        }
        if (auto* c = std::get_if<client::Closed>(&*e)) {
            std::fprintf(stderr, "connection closed: %s\n", c->reason.c_str());
            return std::nullopt;
        }
    }
    std::fprintf(stderr, "timed out\n");
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"sb-cli: server browser command-line client"};
    app.require_subcommand(1);
    std::string server = "127.0.0.1:4433", ca;
    bool insecure = false, no_datagrams = false;
    app.add_option("-s,--server", server, "edge address host:port");
    app.add_flag("-k,--insecure", insecure, "skip certificate validation (development)");
    app.add_option("--ca", ca, "CA certificate file");
    app.add_flag("--no-datagrams", no_datagrams, "use the stream fallback for deltas");

    // browse
    auto* browse = app.add_subcommand("browse", "subscribe to a live view and print every change");
    std::string sort = "players", version, pwd = "any";
    u32 window = 50, seconds = 0;
    bool table = false;
    browse->add_option("--sort", sort, "players | newest | name");
    browse->add_option("--version", version, "only this game version (major.minor)");
    browse->add_option("--password", pwd, "any | none | only");
    browse->add_option("--window", window, "top-K size");
    browse->add_option("--seconds", seconds, "stop after N seconds (0: run until Ctrl-C)");
    browse->add_flag("--table", table, "redraw the whole window on every change");

    // query
    auto* query = app.add_subcommand("query", "search and page through a view");
    std::string text;
    u32 limit = 50;
    bool all_pages = false;
    query->add_option("text", text, "substring to search for (name, author or id)");
    query->add_option("--sort", sort, "players | newest | name");
    query->add_option("--version", version, "only this game version");
    query->add_option("--limit", limit, "page size");
    query->add_flag("--all", all_pages, "follow cursors to the end");

    // resolve / join
    auto* resolve = app.add_subcommand("resolve", "show one entry by id (deep links)");
    std::string id_str;
    resolve->add_option("id", id_str, "entry uuid")->required();
    auto* join = app.add_subcommand("join", "obtain the game server address");
    std::string password;
    join->add_option("id", id_str, "entry uuid")->required();
    join->add_option("--password", password, "server password");

    // host
    auto* host = app.add_subcommand("host", "register a game server and keep it alive");
    std::string name = "My Server", description, game_version = "4.5", author = "sb-cli", token_file = "sb-host.token";
    u32 game_port = 7777, max_players = 100, players = 0;
    bool hidden = false, simulate = false;
    host->add_option("--id", id_str, "entry uuid (default: random, persisted next to the token)");
    host->add_option("--name", name);
    host->add_option("--description", description);
    host->add_option("--game-version", game_version);
    host->add_option("--author", author);
    host->add_option("--game-port", game_port);
    host->add_option("--password", password);
    host->add_option("--max-players", max_players);
    host->add_option("--players", players);
    host->add_option("--token-file", token_file, "where the ownership token is stored");
    host->add_flag("--hidden", hidden, "do not list publicly (reachable through deep links only)");
    host->add_flag("--simulate", simulate, "change the player count every second");

    CLI11_PARSE(app, argc, argv);
    std::signal(SIGINT, [](int) { g_stop = true; });

    try {
        auto [h, port] = ops::split_host_port(server);
        client::ClientRuntime rt({.insecure = insecure, .ca_file = ca, .keepalive_ms = 5000});
        Inbox inbox;
        client::Client::Options co;
        co.host = h;
        co.port = static_cast<u16>(port);
        co.role = host->parsed() ? wire::Role::host : wire::Role::browser;
        co.features = no_datagrams ? wire::feature::zstd : wire::feature::datagrams | wire::feature::zstd;
        co.client_version = "sb-cli/0.1";
        co.on_event = [&](client::Event& e) { inbox.push(std::move(e)); };
        client::Client c(rt, co);
        c.connect();
        auto welcome = await<wire::Welcome>(inbox);
        if (!welcome) return 2;

        wire::ViewSpec view{.bucket = version.empty() ? wire::kBucketAll : registry::version_bucket(version),
                            .password = parse_pwd(pwd),
                            .sort = parse_sort(sort)};

        if (browse->parsed()) {
            c.subscribe(view, window);
            client::ViewMirror mirror;
            const auto start = std::chrono::steady_clock::now();
            while (!g_stop && (!seconds || std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds))) {
                auto e = inbox.pop(200ms);
                if (!e) continue;
                bool changed = false;
                if (auto* s = std::get_if<client::SnapshotEvent>(&*e)) {
                    mirror.on_snapshot(s->snapshot);
                    std::printf("snapshot: %zu of %u entries (vseq %llu)\n", s->snapshot.entries.size(), s->snapshot.total,
                                static_cast<unsigned long long>(s->snapshot.vseq));
                    changed = true;
                } else if (auto* d = std::get_if<client::DeltaEvent>(&*e)) {
                    mirror.on_delta(d->delta);
                    changed = true;
                    if (!table)
                        for (const auto& p : d->delta.patches) {
                            std::printf("%s vseq=%llu handle=%llu", d->via_datagram ? "dgram " : "stream",
                                        static_cast<unsigned long long>(p.vseq), static_cast<unsigned long long>(p.handle));
                            if (p.removed) std::printf(" removed");
                            if (p.entry) std::printf(" entry='%s'", p.entry->name.c_str());
                            if (p.players) std::printf(" players=%u", *p.players);
                            if (p.name) std::printf(" name='%s'", p.name->c_str());
                            std::printf("\n");
                        }
                } else if (auto* g = std::get_if<wire::GoAway>(&*e)) {
                    std::printf("server asked us to reconnect within %u ms\n", g->reconnect_after_ms);
                } else if (auto* cl = std::get_if<client::Closed>(&*e)) {
                    std::printf("closed: %s\n", cl->reason.c_str());
                    break;
                } else if (auto* err = std::get_if<wire::Error>(&*e)) {
                    std::fprintf(stderr, "error: %s\n", err->message.c_str());
                }
                if (changed && table) {
                    std::printf("\x1b[2J\x1b[H%zu servers in window\n", mirror.size());
                    for (const auto& en : mirror.sorted(view.sort)) print_entry(en);
                }
            }
        } else if (query->parsed()) {
            wire::Bytes cursor;
            do {
                c.query(view, text, limit, cursor);
                auto r = await<wire::QueryResult>(inbox);
                if (!r) return 2;
                for (const auto& en : r->entries) print_entry(en);
                std::printf("-- %zu shown, %u total\n", r->entries.size(), r->total);
                cursor = r->next_cursor;
            } while (all_pages && !cursor.empty());
        } else if (resolve->parsed() || join->parsed()) {
            auto id = Uuid::parse(id_str);
            if (!id) {
                std::fprintf(stderr, "invalid uuid\n");
                return 2;
            }
            if (resolve->parsed()) {
                c.resolve(*id);
                auto r = await<wire::ResolveResult>(inbox);
                if (!r) return 2;
                if (!r->details) {
                    std::printf("not found\n");
                    return 1;
                }
                print_entry(r->details->entry);
                std::printf("description: %s\n", r->details->description.c_str());
            } else {
                c.join(*id, password.empty() ? std::nullopt : std::optional<std::string>(password));
                auto g = await<wire::JoinGrant>(inbox);
                if (!g) return 2;
                std::printf("%s:%u ticket=%s\n", addr_string(g->address).c_str(), g->port, hex(g->ticket).c_str());
            }
        } else if (host->parsed()) {
            wire::HostRegister reg{.name = name, .description = description, .version = game_version, .author = author,
                                   .game_port = game_port, .max_players = max_players, .hidden = hidden, .players = players};
            if (!password.empty()) reg.password = password;
            // The token file stores "uuid token-hex"; reusing it keeps the same entry across restarts.
            {
                std::ifstream f(token_file);
                std::string fid, ftok;
                if (f >> fid >> ftok && ftok.size() == 64) {
                    if (id_str.empty()) id_str = fid;
                    wire::Token t{};
                    for (std::size_t i = 0; i < 32; ++i) t[i] = static_cast<u8>(std::stoi(ftok.substr(i * 2, 2), nullptr, 16));
                    reg.token = t;
                }
            }
            if (id_str.empty()) {
                security::random_bytes(reg.id.bytes);
                reg.id.bytes[6] = static_cast<u8>((reg.id.bytes[6] & 0x0F) | 0x40);
                reg.id.bytes[8] = static_cast<u8>((reg.id.bytes[8] & 0x3F) | 0x80);
            } else if (auto id = Uuid::parse(id_str)) {
                reg.id = *id;
            } else {
                std::fprintf(stderr, "invalid uuid\n");
                return 2;
            }
            c.host_register(reg);
            auto r = await<wire::HostRegistered>(inbox, 10s);
            if (!r) return 2;
            if (r->token) {
                std::ofstream f(token_file, std::ios::trunc);
                f << reg.id.to_string() << ' ' << hex(*r->token) << '\n';
                std::printf("new ownership token saved to %s\n", token_file.c_str());
            }
            std::printf("registered %s (handle %llu); deep link: Reboot://%s\n", reg.id.to_string().c_str(),
                        static_cast<unsigned long long>(r->handle), reg.id.to_string().c_str());
            std::mt19937 rng{std::random_device{}()};
            auto next_hb = std::chrono::steady_clock::now();
            auto next_sim = next_hb;
            const auto hb_every = std::chrono::milliseconds(std::max<u32>(1000, welcome->limits.heartbeat_ms / 2));
            while (!g_stop) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= next_hb) {
                    c.heartbeat();
                    next_hb = now + hb_every;
                }
                if (simulate && now >= next_sim) {
                    players = static_cast<u32>(rng() % (max_players + 1));
                    c.host_update(wire::HostUpdate{.players = players}, false);
                    next_sim = now + 1s;
                }
                if (auto e = inbox.pop(100ms)) {
                    if (auto* s = std::get_if<wire::HostStatus>(&*e))
                        std::printf("reachability: %s (failures %u)\n", s->reachable ? "ok" : "unreachable", s->probe_failures);
                    else if (auto* err = std::get_if<wire::Error>(&*e))
                        std::fprintf(stderr, "error: %s\n", err->message.c_str());
                    else if (auto* g = std::get_if<wire::GoAway>(&*e))
                        std::printf("edge draining; reconnect within %u ms\n", g->reconnect_after_ms);
                    else if (std::holds_alternative<client::Closed>(*e))
                        break;
                }
            }
            c.host_unregister();
            (void)await<wire::Ack>(inbox, 2s);
        }
        c.close();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sb-cli: %s\n", e.what());
        return 1;
    }
    return 0;
}
