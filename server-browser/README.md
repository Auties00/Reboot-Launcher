# server-browser

`server-browser` is the real-time server browser for Reboot. Hosts publish their game servers to it, and players browse and join them. Every change reaches the people looking at it as soon as it happens, with no polling and no batching tick.

It is written in C++26 on MsQuic and scales out across any number of edges, each holding the whole registry, kept in sync over NATS JetStream. It replaces the Dart WebSocket backend from the `refactor` branch. That backend broadcast every change to every client, never expired dead hosts, let anyone overwrite anyone's entry, and published password hashes along with encrypted IPs.

- [Quick start](#quick-start)
- [Protocol](#protocol): the contract for client implementers (`rbsb/1`)
- [Architecture](#architecture): how fan-out, replication and failure handling work
- [Operations](#operations): build, deploy, tune, monitor and load-test

## Quick start

```sh
docker build -f deploy/docker/Dockerfile --target test .     # GCC 16 build + all tests
docker compose -f deploy/compose.yaml up --build             # 3 NATS nodes + 3 edges
```

From inside the compose network, or with the binaries built locally:

```sh
sb-cli -k -s 127.0.0.1:4431 browse --table           # live view on edge 1
sb-cli -k -s 127.0.0.1:4432 host --name Test --simulate   # host on edge 2, player counts every second
```

## Protocol

This is the contract between `sb-edge` and its clients (the launcher's browser and its game-server hosts). The message schema is [`proto/rbsb.proto`](proto/rbsb.proto). Any protobuf runtime can decode it. Byte-exact examples live in [`tests/vectors`](tests/vectors). `src/client/` is the C++ reference client, and `src/client/view_mirror.hpp` implements the delta rules described below.

### Transport

| | |
|---|---|
| Transport | QUIC v1 (RFC 9000) over UDP, default port 443 |
| ALPN | `rbsb/1`; the major version lives in the ALPN, the minor in `Hello.proto_minor` |
| TLS | TLS 1.3, server certificate for the public hostname. 0-RTT is not accepted; session resumption is. |
| Datagrams | RFC 9221 DATAGRAM frames. Clients must enable receiving them to get deltas over datagrams. |
| Client stream limits | Allow the server to open at least 8 unidirectional streams. The server never accepts client-opened unidirectional streams, and only one bidirectional stream. |

A connection uses three kinds of channel:

- **Control stream.** The first and only bidirectional stream, opened by the client. Requests and responses travel on it in both directions; responses carry the request's `req_id`.
- **Snapshot streams.** Server-opened unidirectional streams. Each carries exactly one `Snapshot` (or `SnapshotZstd`) frame, followed by FIN.
- **Datagrams.**
  - server to client: `Delta` frames;
  - client to server: `HostHeartbeat`.

  A datagram may carry several frames back to back.

If the client does not set the `datagrams` feature bit, the server opens one long-lived unidirectional **delta stream** right after `Welcome` and writes `Delta` frames to it instead. The rules are the same, except that nothing is lost or reordered.

### Framing

```
frame := type (QUIC varint) | length (QUIC varint) | payload (protobuf message, `length` bytes)
```

Lengths may use a non-minimal varint width; the server uses two bytes in datagrams and four elsewhere. Unknown frame types on the control stream are answered with `Error{code: UNSUPPORTED}`; elsewhere they are ignored. Unknown protobuf fields must be skipped. Frames above 16 KiB on the control stream close the connection.

| type | message | direction | channel |
|---|---|---|---|
| 1 | Hello | C→S | control (must be first) |
| 2 | Subscribe | C→S | control |
| 3 | Unsubscribe | C→S | control |
| 4 | Query | C→S | control |
| 5 | Resolve | C→S | control |
| 6 | Join | C→S | control |
| 7 | HostRegister | C→S | control |
| 8 | HostUpdate | C→S | control |
| 9 | HostUnregister | C→S | control |
| 32 | Welcome | S→C | control |
| 33 | SubOpen | S→C | control |
| 34 | QueryResult | S→C | control |
| 35 | ResolveResult | S→C | control |
| 36 | JoinGrant | S→C | control |
| 37 | HostRegistered | S→C | control |
| 38 | Ack | S→C | control |
| 39 | Error | S→C | control |
| 40 | GoAway | S→C | control |
| 41 | HostStatus | S→C | control |
| 48 | Snapshot | S→C | snapshot stream |
| 49 | SnapshotZstd | S→C | snapshot stream; payload is `varint raw_length` followed by zstd(Snapshot) |
| 64 | Delta | S→C | datagram (or delta stream) |
| 65 | HostHeartbeat | C→S | datagram (or control) |

### Session

1. The client opens the control stream and sends `Hello{role, proto_minor: 0, client_version, features}`.
   - `role`: BROWSER or HOST.
   - `features`: bit 1 = datagrams, bit 2 = zstd snapshots.
2. The server answers with `Welcome`:
   - the features it accepted;
   - `server_time_ms`, for clock-skew-free display;
   - `limits`: heartbeat interval, TTL, maximum subscriptions and window, maximum query page, host update rate.

   Clients must respect these limits.
3. At any time the server may send `GoAway{reason, reconnect_after_ms}`.
   - The client should reconnect after a random delay within `reconnect_after_ms`, re-resolving DNS (it will probably reach another edge).
   - Hosts re-register with their token.
   - The current connection keeps working until the server closes it.

`Error{req_id, code, message, retry_after_ms}` answers a failed request. `RATE_LIMITED` errors carry a `retry_after_ms` hint.

### Browsing

#### Views

A view is a `ViewSpec`:

| field | meaning |
|---|---|
| `bucket` | game version: 0 = all, 1 = unparseable versions, else `2 + major * 1024 + minor` from the leading `major[.minor]` digits of the version string (`"4.5"` → 4103, `"1.7.2"` → 1033) |
| `password` | ANY, NO_PASSWORD or PASSWORD_ONLY |
| `region` | continent of the host (0 = all) |
| `sort` | PLAYERS, NEWEST or NAME |

A view lists only entries that are online, reachable and not hidden. Its order is total, and clients must sort with exactly these rules:

- **PLAYERS:** `players` descending, then `floor(created_ms / 1000)` descending, then `handle` ascending.
- **NEWEST:** `created_ms` descending, then `handle` ascending.
- **NAME:** name with ASCII letters lowercased, compared bytewise ascending, then `handle` ascending.

#### Subscriptions: live top-K windows

`Subscribe{req_id, sub_id, view, window}` asks for the first `window` entries of a view. The server rounds `window` up to a supported size (50 or 200) and answers with `SubOpen{req_id, sub_id, view_id, window}`. It then sends a `Snapshot{view_id, vseq, total, entries}` on a new snapshot stream, and from then on `Delta{view_id, patches}` for every change to that window.

- `view_id` names the server-side view. Several subscriptions (even from other clients) can share one; a client keeps one mirror per `view_id`.
- `total` is the number of entries in the whole view, not just the window.
- A browser that needs "every version I have installed" subscribes to several single-version views and merges them locally.
- `Unsubscribe{sub_id}` stops a subscription.

There is no batching interval. A change is sent as soon as it happens.

#### Applying deltas

Datagrams can be lost, duplicated or reordered, and the server repairs losses by resending *current* values. To converge, clients must apply patches with **per-field sequence numbers**. For each entry (keyed by `handle` within a view) keep:

```
seq_member   presence plus the fields only a full entry carries (id, author, version, bucket, region, created_ms)
seq_players  players and max_players
seq_flags    flags
seq_name     name
```

The view also has a `floor`, which is the `vseq` of its latest snapshot. For a patch `p` with sequence `v = p.vseq`:

1. If `v <= floor`, ignore it.
2. If `p.removed`: when `v > seq_member`, mark the entry absent and raise **all four** sequences to `v`. Keep the tombstone so an older insert cannot resurrect the entry.
3. If `p.entry` is present (a full entry): when `v >= seq_member`, the entry becomes present, all full-entry fields are replaced, and `seq_member = v`. Each grouped field is also replaced when `v >=` its own sequence, which is then set to `v`. Note `>=`: repairs may repeat the latest sequence.
4. Each partial field (`players`/`max_players`, `flags`, `name`) is replaced when `v >` its sequence, which is then set to `v`.

Partial fields may arrive for an entry the client does not have (its full entry was lost). Store them anyway: the repair brings the full entry later.

**On `Snapshot`:**

- Replace the whole view with the snapshot entries, setting all sequences to `snapshot.vseq`.
- Set `floor = snapshot.vseq`.
- Replay patches that arrived before the snapshot, applying only those above the floor. Patches can overtake the snapshot because they travel on different channels, so buffer deltas for a `view_id` until its first snapshot arrives.
- The server may send a fresh snapshot for an existing subscription at any time. Treat it the same way.

**On `Delta.sync` (`WindowSync{vseq, handles}`):** a client that fell behind is not sent every removal it missed. Instead, a delta carries the complete list of handles in the window at `vseq`. Apply it before that frame's patches:

- every present entry that is **not** listed and whose `seq_member < vseq` is removed, raising all four sequences to `vseq`;
- listed entries are kept; the patches that follow carry current values for members that changed or that the client may lack.

Buffer a sync that arrives before the view's first snapshot like patches (only the newest one matters).

The window holds at most `window` present entries once the client has caught up. In between, extra entries may briefly show; sort and truncate when displaying.

#### Queries: deep pages and search

`Query{req_id, view, text, limit, cursor}` returns one page as `QueryResult{entries, next_cursor, total}`.

- Without `text`, it pages through the view in its sort order.
- With `text`, it returns entries of the view whose case-folded name, author or id contains `text` (1 to 64 bytes).
- `cursor` is opaque: pass back `next_cursor` to get the next page. An empty `next_cursor` means the end.
- Queries are not live. Re-run them, or subscribe for live data.
- `limit` is capped by `Welcome.limits.max_query_limit`; 0 means 50.
- Rate limit: about 10 queries per second per connection, with bursts.

#### Resolve: deep links

`Resolve{req_id, id}` returns `ResolveResult{details?}` with the full entry, its description and `updated_ms`. This works for hidden and offline entries too, which is how `Reboot://<uuid>` links are resolved.

#### Join

`Join{req_id, id, password?}` returns `JoinGrant{address, port, ticket, expires_ms}`. This is the **only** message that ever carries a server's address.

- The address is 4 bytes for IPv4 or 16 for IPv6, network order.
- If the entry is password-protected and the password is missing or wrong, the server answers `Error{WRONG_PASSWORD}`.
- Attempts are limited to about 5 per minute per (client address, entry).
- `ticket` is an HMAC over (id, address, port, expiry). It is reserved for game servers that want to admit only clients that went through the browser.

### Hosting

1. **Register.** Connect with `role = HOST` and send `HostRegister`:
   - `id`: a UUID you generate once and keep.
   - `token`: absent the very first time.
   - `name` (1 to 64 bytes), `description` (up to 256), `version` (1 to 16), `author` (up to 32).
   - `game_port`: the UDP port of the game server.
   - `password`: optional; an empty string means none.
   - `max_players`, `players`, `hidden`.

   The answer is `HostRegistered{token?, handle, heartbeat_ms, ttl_ms, observed_address}`.

   **Store the token.** It is returned only once and proves ownership of `id`. Without it the id cannot be registered again, not even after a crash or from another edge, for 30 days. The server derives the address from the connection itself; hosts never send it.
2. **Reachability.**
   - The server sends one UDP packet to `observed_address:game_port`: 25 bytes, `0x01`, 23 zero bytes, `0x04`.
   - Any reply proves the port is reachable. Unproven entries stay hidden until the first reply arrives.
   - The probe repeats every few minutes, and an entry is hidden after 3 consecutive failures.
   - `HostStatus{reachable, probe_failures}` reports the result.
3. **Heartbeat.** Send a `HostHeartbeat` datagram every `heartbeat_ms / 2` or so. After `ttl_ms` without one, the entry goes offline. A closed connection takes it offline immediately.
4. **Update.** `HostUpdate` changes only the fields it carries.
   - Send player counts the moment they change, with `req_id = 0`: no Ack, and when rate limited they are merged so the newest value wins.
   - The limit is `limits.host_update_burst` and `host_update_per_sec`. A request with a `req_id` that exceeds it gets `RATE_LIMITED`.
   - `password: ""` removes the password.
5. **Unregister.** `HostUnregister` removes the entry. Disconnecting without it keeps the entry offline but recoverable for the grace period (15 s), after which it is removed.
6. **Superseded.** If the same id is registered with its token on another connection (a restart, or a move to another edge), the old connection gets `Error{CONFLICT}` and is closed.

### Errors

| code | meaning |
|---|---|
| BAD_REQUEST | invalid field (the message names it), invalid view or invalid cursor |
| UNSUPPORTED | unknown frame type |
| RATE_LIMITED | slow down; see `retry_after_ms` |
| NOT_FOUND | no such entry |
| WRONG_PASSWORD | join password missing or wrong |
| UNAUTHORIZED | id owned by someone else (token missing or wrong), or an operation not allowed for this role |
| UNREACHABLE | the entry's game port does not answer |
| LIMIT_EXCEEDED | too many subscriptions, or too many servers from one address |
| CONFLICT | concurrent registration; retry after `retry_after_ms` |
| UNAVAILABLE | entry offline, or the edge is not ready |

### Client checklist

- Enable QUIC datagram reception, allow at least 8 server-opened unidirectional streams, and set the ALPN to `rbsb/1`.
- Validate the server certificate.
- Keep one control stream. Assign `req_id` values and match responses by them.
- Buffer deltas until a view's snapshot arrives. Apply window syncs, then patches, with per-field sequences. Accept a fresh snapshot at any time.
- Connect only while the browser screen is open, or while hosting. Idle connections cost the server memory and buy nothing.
- On `GoAway` or a lost connection, reconnect with exponential backoff and jitter, then resubscribe. Hosts re-register with their token.
- Hosts: persist `(id, token)`, heartbeat over datagrams, and push player counts as they change.

## Architecture

`sb-edge` keeps a live list of community game servers ("entries") and pushes every change to the people browsing it, as soon as the change happens. It is designed for millions of concurrently connected browsers and up to about 10⁵ hosts.

### The shape of the problem

The data is small and the audience is huge. 100k entries at about 400 B each is roughly 40 MB, changing a few thousand times per second. The cost is all in **fan-out**: one player-count change may concern every browser looking at that server's view.

Three decisions follow from that:

1. **Every edge holds a full replica.** Browse, search, resolve and join are answered locally. There is no read-side sharding and no connection-ID routing: any edge can serve any client, so DNS round-robin is enough.
2. **Event-driven delivery, never timers.** A change is encoded once per affected view and handed to MsQuic immediately. Coalescing only happens when a client's connection is already busy (autocork), and then the client gets the *current* value, never a stale backlog.
3. **Two replication paths.**
   - **Lifecycle** changes are rare and must be durable and totally ordered: register, unregister, ownership, address, password, version.
   - **Soft** changes are hot and latest-value-wins: players, name, description, visibility.

### Process layout

```
                         ┌──────────────── sb-edge ─────────────────────────────────────────┐
  hosts & browsers       │                                                                  │
  ───── QUIC/UDP ───────▶│  shard 0 … N-1  (one pinned thread per core)                     │
                         │  ┌─────────────────────────────────────────────────────────────┐ │
                         │  │ MsQuic app-owned execution (epoll) ── connection callbacks   │ │
                         │  │ control protocol · rate limits · host TTL timers             │ │
                         │  │ fan-out: ring events → DatagramSend / dirty sets → flushes   │ │
                         │  └───────┬──────────────────────────────────────▲──────────────┘ │
                         │   MPSC inbox (requests)               broadcast ring (events,    │
                         │          │                            frames, ordered replies)   │
                         │  ┌───────▼──────────────────────────────────────┴──────────────┐ │
                         │  │ replica thread: records, order-statistic views, frames,      │ │
                         │  │ snapshots, keyset queries, joins, lifecycle state machine    │ │
                         │  └───────▲───────────────────────────────┬──────────────────────┘ │
                         │          │ deliveries                    │ writes                 │
                         │  ┌───────┴───────────────┐   ┌───────────▼───────────┐           │
                         │  │ backbone (NATS)       │   │ search workers        │           │
                         │  │ JetStream + core NATS │   │ trigram + roaring     │           │
                         │  └───────────────────────┘   └───────────────────────┘           │
                         │  prober (UDP reachability) · admin HTTP (/metrics, /readyz)       │
                         └──────────────────────────────────────────────────────────────────┘
```

#### Shards

A shard is a thread pinned to one core that runs one MsQuic execution context: `ExecutionCreate`, then `ExecutionPoll` plus `epoll_wait` on the same queue. The shard's wakeup eventfd sits in that queue too, so MsQuic I/O, ring consumption and dirty-set flushes all happen on one thread with no cross-thread hops.

- **Connection ownership.** MsQuic invokes a connection's callbacks on the worker that owns its partition. The listener callback runs there too, so the shard that accepts a connection owns its state.
- **Migration.** Client migration is disabled, which keeps the 4-tuple, and so the partition, stable. If MsQuic still moves a connection, its events are forwarded to the home shard's mailbox in order (`Conn::moved`).

#### Replica

The replica is the single writer. It owns `Record`s, the uuid→handle index and, per **partition**, a counted B+tree for each sort (`core/counted_btree.hpp`). A partition is one (version bucket | all, password filter, region | all) combination.

- A visible entry sits in at most 8 partitions × 3 sorts = 24 trees.
- A **view** is (partition, sort, window K). Window membership changes are computed with order statistics, in O(log n) per affected view:

| transition | patches emitted |
|---|---|
| in → in | partial patch (changed fields only) |
| out → in | full entry, plus removal of the entry pushed to rank K |
| in → out | removal, plus full entry of the one pulled up to rank K−1 |

Each mutation produces at most one encoded `Delta` frame per affected view, shared by every subscriber of that view on every shard.

Published state for other threads is immutable. Each change creates a new `PubEntry` (and, when a window's membership changes, a new window list), swaps it into an atomic table and retires the old one. Ring cursors double as quiescent-state counters: a retired object is freed once every consumer has passed the event that retired it.

Requests that must be ordered with mutations (subscribe, which needs snapshot plus registration at the exact ring position) travel back on the ring. Text searches go to search workers, which keep their own trigram index fed from the same ring.

#### Fan-out, step by step

1. A host sends `HostUpdate{players}`. Its shard computes nothing heavy and posts the update to the replica inbox (an MPSC queue plus a wakeup).
2. The replica:
   1. stamps `ver = (lease_epoch, owner_seq)` and applies the change;
   2. moves the entry in its 8 "players" trees;
   3. bumps `vseq` for each affected view and encodes one ~70–100 B `Delta` frame per view;
   4. publishes one ring event;
   5. publishes the soft update to core NATS for other edges.
3. Each shard reads the event and, for every local subscriber of each frame's view, does one of two things:
   - **Idle connection** (no unsent datagram): `DatagramSend(shared frame)`. Zero copy, because `SendBufferingEnabled = 0` and frames are refcounted per shard (one atomic per shard, not per send). Frames for the same connection in one batch use `QUIC_SEND_FLAG_DELAY_SEND`, so MsQuic packs them into one packet.
   - **Busy connection:** mark `(view, entry, fields)` in its dirty set. When MsQuic reports the previous datagram as SENT, the shard flushes the dirty set as one datagram built from current values.
4. Lost datagrams (`LOST_SUSPECT` / `LOST_DISCARDED`) re-mark their entries dirty, so repairs always carry current values. Patches carry per-field sequence numbers, so reordering and duplication are harmless (see [Applying deltas](#applying-deltas)).
5. When a busy connection's dirty set for a view grows past 2 × window (window churn produces a stream of entries leaving), the shard drops every pending removal and owes the client one `WindowSync`: the handles currently in the window, about 3 bytes each. The client removes everything not listed, and the remaining dirty entries (all window members) carry the new values. Memory per slow client is bounded by its windows, and catching up costs a few hundred bytes instead of a snapshot. (An earlier design resent a snapshot here; the load test showed it turning congestion into a snapshot storm.)

### Replication

| path | transport | used for | ordering |
|---|---|---|---|
| lifecycle | JetStream stream, one subject per entry, `MaxMsgsPerSubject = 1`, R3 | register, unregister, ownership moves, address/port, password, version, debounced snapshots of soft fields | stream sequence; writes use compare-and-set on the subject's last sequence |
| soft | core NATS, `SendAsap`, no echo | players, name, description, hidden, online, reachable | per-entry `ver`; receivers keep the newest |
| leases | JetStream KV with TTL | edge liveness | a lapsed key marks an edge dead; rendezvous hashing picks one reaper |

Every edge applies lifecycle records in stream order, including its own writes (the write's ack and the record's delivery are joined before replying). Soft updates are applied only if their `ver` is newer, so a late, stale message can never undo a newer one.

On startup an edge does three things:

1. subscribes to the soft path;
2. replays the lifecycle stream (`DeliverLastPerSubject`), holding soft updates that arrive before their record;
3. only then reports ready.

**Ownership.**

- The first registration returns a 256-bit token, and only its SHA-256 is stored.
- Any later registration of the same id needs the token. It takes over the entry with a lifecycle compare-and-set: the epoch goes up and the old connection is told it was superseded.
- That covers both a host restarting and a host moving to another edge after a drain, without the entry ever disappearing from browsers.

**Failures.**

- **Host connection closes:** the entry goes offline immediately (soft path), then is tombstoned after the grace period unless it re-registers.
- **Heartbeat stops:** the same, after `ttl_ms`.
- **Edge crashes:** every surviving edge hides its entries as soon as its lease lapses. The elected reaper tombstones whatever did not re-register within the grace period.
- **NATS partition:** edges keep serving their replicas. Soft updates resume on reconnect, and the debounced persistence (≤ 1 s old) repairs anything missed.

### Security

- **Addresses are never listed.** A server's address and port are revealed only by `Join`, after the password check. The address is the one the edge observed, never one the host claims.
- **Passwords:** stored as `HMAC-SHA256(pepper, id ‖ password)`. The pepper exists only on edges, never in NATS, so a stolen backbone does not reveal passwords. Checks cost about 1 µs, and joins are rate limited per (address, entry).
- **Reachability probes** only target the address the host connected from, at a bounded global rate, with the launcher's own ping payload. They cannot be used for reflection.
- **Rate limits:**
  - per-address and per-subnet connection buckets on the accept path (lock-free, fixed memory);
  - per-connection query and host-update buckets;
  - per-(address, entry) join buckets.
- **Input validation:** strict UTF-8 with no control characters, size limits on every field, frame and backlog caps.
- **TLS:**
  - 0-RTT off;
  - resumption tickets and stateless-retry keys shared across edges;
  - certificates reloaded on SIGHUP without dropping connections.

### C++26 usage

| feature | where | fallback |
|---|---|---|
| P2996 static reflection | `wire/codec.hpp` generates the protobuf codec from plain structs | Boost.PFR (identical output, checked by golden vectors) |
| P2900 contracts | `SB_PRE` / `SB_ASSERT` | assert-style macros |
| `std::inplace_vector` | small fixed-capacity sets (frame items, subscriptions) | `boost::container::static_vector` |
| `std::atomic::wait`, `std::jthread`, `std::stop_token` | wakeups and thread lifecycle | — |

CMake probes each feature (`cmake/SbFeatures.cmake`), and `std::execution` is not used. The hot path is a hand-written actor pipeline (MPSC inbox → single writer → broadcast ring), which needs no generic scheduler.

### Performance notes

- **Per mutation:** O(log n) tree updates for each of up to 24 trees, plus at most about 48 frame encodes. The replica handles tens of thousands of mutations per second on one core.
- **Per delivery:** one `DatagramSend` of a shared buffer. The kernel and crypto path, about 1.5–3 µs per packet, dominates. MsQuic's epoll datapath already batches receives with `recvmmsg` and uses UDP GSO for sends and GRO for receives; beyond that the per-core ceiling rises with NIC RSS queues and IRQ affinity (see [Datapath](#datapath) for why XDP and io_uring are not options).
- **Memory per idle browser** is about 100 KiB, almost all of it MsQuic and TLS connection state. Our own per-connection state is a small struct and a dirty set that stays empty until the connection falls behind. Snapshots are shared buffers, cached per (view, vseq).
- **Handshakes:**
  - ECDSA P-256 certificates;
  - resumption tickets valid across edges;
  - Retry under memory pressure;
  - `MaxWorkerQueueDelayUs` sheds new handshakes before delivery latency suffers.

## Operations

### Build

The supported toolchain is Linux with GCC 16 (or Clang 21+ for sanitizer and fuzz builds), CMake ≥ 3.30, vcpkg, and MsQuic ≥ 2.5 built against the system OpenSSL 3.5. The Dockerfile pins all of it:

```sh
docker build -f deploy/docker/Dockerfile --target test .             # build plus unit and integration tests
docker build -f deploy/docker/Dockerfile -t sb-edge .                # runtime image (distroless)
```

On a Linux host with the same toolchain:

```sh
cmake --preset gcc-release && cmake --build --preset gcc-release && ctest --preset gcc-release
cmake --preset clang-asan  && cmake --build --preset clang-asan  && ctest --preset clang-asan
./build/clang-asan/fuzz/fuzz_messages -max_total_time=60
```

`SB_NATS_URL=nats://127.0.0.1:4222` enables the multi-edge tests, which need nats-server ≥ 2.11 with JetStream. `SB_GEOIP_DB` enables the GeoIP lookup tests.

### Run

**Single node, development** (self-signed certificate, no probes, inproc backbone):

```sh
sb-edge --listen 127.0.0.1:4433 --admin 127.0.0.1:9100 --self-signed --no-probe --plain-logs
sb-cli -k -s 127.0.0.1:4433 host --name "Test" --simulate        # in another terminal
sb-cli -k -s 127.0.0.1:4433 browse --table
```

**Local cluster** (3 NATS nodes, 3 edges): `docker compose -f deploy/compose.yaml up --build`.

**Production:** `deploy/config/edge.example.toml` plus `deploy/systemd/sb-edge.service`.

- Generate the shared secrets once and distribute them to every edge:
  ```sh
  head -c 64 /dev/urandom > /etc/sb/pepper            # password HMAC key: changing it invalidates every stored password
  head -c 64 /dev/urandom > /etc/sb/join-ticket.key   # join tickets
  head -c 64 /dev/urandom > /etc/sb/ticket.key        # TLS resumption across edges (16-byte id + 48 bytes)
  head -c 32 /dev/urandom > /etc/sb/retry.key         # stateless retry across edges
  chmod 0400 /etc/sb/*.key /etc/sb/pepper
  ```
- **TLS:** Let's Encrypt ECDSA P-256 for the public name. `systemctl reload sb-edge` (SIGHUP) swaps in the renewed certificate for new connections without dropping existing ones.
- **DNS:** one A/AAAA record per edge under one name, with health checks against `http://<edge>:9100/readyz`. Keep the admin port reachable only from the health checker and Prometheus.
- **NATS:** three nodes (≥ 2.11), JetStream on, mTLS between edges and NATS on a private network. The lifecycle stream and the lease bucket are created by the first edge (`replicas = 3`).

### Signals

| signal | effect |
|---|---|
| SIGTERM | drain: `/readyz` turns 503 → wait `drain_delay_ms` → GoAway to every client, jittered over `drain_spread_ms` → close the rest → exit |
| SIGINT | fast stop: close every connection, exit |
| SIGHUP | reload the TLS certificate and the GeoIP database for new connections |

**Rolling upgrade:** drain one edge at a time. Hosts move to other edges with their tokens, by compare-and-set and with no tombstone, so browsers never see them disappear.

### Kernel and NIC tuning

Apply `deploy/sysctl/99-sb-edge.conf`. Then:

- **conntrack:** never track the QUIC port. Millions of UDP flows overflow it.
  ```sh
  nft add table inet raw
  nft add chain inet raw prerouting '{ type filter hook prerouting priority raw; }'
  nft add chain inet raw output '{ type filter hook output priority raw; }'
  nft add rule inet raw prerouting udp dport 443 notrack
  nft add rule inet raw output udp sport 443 notrack
  ```
- **RSS:**
  - one RX queue per shard (`ethtool -L <if> combined <shards>`);
  - spread flows by 4-tuple (`ethtool -N <if> rx-flow-hash udp4 sdfn`, same for `udp6`);
  - pin each queue's IRQ to the core of the shard with the same index;
  - disable irqbalance.
- **Interrupt coalescing:** `ethtool -C <if> adaptive-rx off rx-usecs 8`, and `echo 2 > /sys/class/net/<if>/napi_defer_hard_irqs`, `echo 50000 > /sys/class/net/<if>/gro_flush_timeout`. Larger values add latency.
- **CPU:** shallow C-states on the shard cores (`cpupower idle-set -D 10`) and the performance governor.
- **NUMA:** on two-socket machines, run one sb-edge per socket (`numactl --cpunodebind=N --membind=N`), each with its own IP. The replica is small enough to duplicate.

#### Datapath

sb-edge uses MsQuic's epoll datapath, which batches receives with `recvmmsg`, segments sends with UDP GSO and coalesces receives with UDP GRO. The two faster-looking alternatives were evaluated against MsQuic 2.6.2 and ruled out:

- **XDP:** MsQuic's XDP datapath exists only for Windows (xdp-for-windows); its documentation states it does not support Linux XDP. The `quic.xdp` option was removed, and a config that still sets it is rejected at startup.
- **io_uring:** MsQuic can be built with an io_uring datapath, but with it the public `QUIC_EVENTQ` type becomes an opaque internal structure, so an application cannot drive it from its own execution loop. sb-edge depends on app-owned execution (one loop per shard runs both MsQuic and fan-out), so io_uring would mean giving that up for MsQuic-managed workers plus a cross-thread hop per send, which costs more than it saves.

The remaining levers are the ones above: RSS queues per shard, IRQ affinity, interrupt coalescing and enough edges.

### GeoIP

Entries are tagged with the continent of the address their host connects from, which feeds the region filter in views. The database is DB-IP "IP to Country Lite" (MaxMind MMDB format, CC BY 4.0, attribution "IP Geolocation by DB-IP"; no account needed):

- `deploy/geoip/update-geoip.sh [dir]` downloads the current month's file and atomically replaces `dir/dbip-country-lite.mmdb`.
- `deploy/systemd/sb-geoip-update.timer` runs it monthly and then `systemctl reload sb-edge`. SIGHUP swaps the database in without dropping connections.
- The container image ships the database at `/usr/share/sb/geoip/dbip-country-lite.mmdb` (refreshed on every image build).
- Without `edge.geoip_db`, every entry's region is unknown (0), so it appears only in "all regions" views.

### Metrics

`GET /metrics` (Prometheus):

| metric | watch for |
|---|---|
| `sb_delivery_seconds` (histogram) | replica-to-datagram handoff; p99 should stay in single-digit milliseconds |
| `sb_connections_browser`, `sb_connections_host` | per shard; uneven shards point at RSS problems |
| `sb_datagrams_direct_total` vs `sb_datagrams_flush_total` | flushes rising means clients or the network can't keep up (autocork engaging) |
| `sb_datagrams_lost_total`, `sb_repairs_total` | network loss |
| `sb_window_syncs_total` | lagging subscriptions caught up with a membership list; rising means clients or their networks cannot keep up |
| `sb_ring_stalls_total` | a shard can't keep up with the replica: add cores or edges |
| `sb_lifecycle_conflicts_total` | concurrent registrations of the same id |
| `sb_rate_limited_total`, `sb_connections_rejected_total` | abuse, or limits that are too tight |
| `sb_process_resident_bytes`, `sb_process_cpu_seconds_total` | capacity: memory per connection, CPU per delivery |
| `msquic_*` | handshake failures, connection queue depth (queue delay sheds handshakes) |

Alert on `/readyz` failures, delivery p99 > 50 ms for 5 minutes, ring stalls > 0, and backbone disconnects (in the logs).

### Load testing and capacity planning

`deploy/loadtest/run.sh` starts one edge and drives five scenarios with `sb-loadgen` on the same machine, then writes a Markdown report: deliveries per second, client and server-side latency, edge and load-generator CPU, edge RSS, autocork ratio and window syncs. The scenarios named in `PROFILE` (by default the 20k and 40k fan-out pair) are also sampled with `perf` for 20 s of steady state, producing a text profile (by thread, shared object and function) and a flame graph. The `server-browser load test` workflow runs it on a GitHub runner weekly and on demand; the report lands in the job summary and everything else in the `loadtest` artifact. Because edge and load generator share one small machine, client-side latency there mostly measures the load generator's own queueing; the edge's server-side handoff latency and its deliveries per core are the meaningful figures.

For capacity planning, run `sb-loadgen` from separate machines against a production-sized edge, with `deploy/config/loadtest.toml` and source addresses spread with `--bind`:

```sh
sb-loadgen -s edge:4433 --hosts 20000 --update-rate 0.5 --browsers 200000 --subs 2 --window 50 \
           --ramp 5000 --duration 600 --sample 0.01 --bind 10.0.0.11 --bind 10.0.0.12 ...
```

`sb-loadgen` prints delivery latency (host update → browser datagram) percentiles every report interval, along with throughput, and `--summary` writes them as JSON. Size the fleet by deliveries per second per edge, not by connections:

```
deliveries/s ≈ mutations/s on hot views × subscribers of those views
```

Scale out by adding edges. Every edge holds the full registry, so edges are interchangeable.

**Keep browsers honest:** the launcher should connect only while the server browser is visible, or while hosting.
