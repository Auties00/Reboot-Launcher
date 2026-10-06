# Architecture

`sb-edge` keeps a live list of community game servers ("entries") and pushes every change to the people browsing it, as soon as the change happens. It is designed for millions of concurrently connected browsers and up to about 10⁵ hosts.

## The shape of the problem

The data is small and the audience is huge. 100k entries at about 400 B each is roughly 40 MB, changing a few thousand times per second. The cost is all in **fan-out**: one player-count change may concern every browser looking at that server's view.

Three decisions follow from that:

1. **Every edge holds a full replica.** Browse, search, resolve and join are answered locally. There is no read-side sharding and no connection-ID routing: any edge can serve any client, so DNS round-robin is enough.
2. **Event-driven delivery, never timers.** A change is encoded once per affected view and handed to MsQuic immediately. Coalescing only happens when a client's connection is already busy (autocork), and then the client gets the *current* value, never a stale backlog.
3. **Two replication paths.**
   - **Lifecycle** changes are rare and must be durable and totally ordered: register, unregister, ownership, address, password, version.
   - **Soft** changes are hot and latest-value-wins: players, name, description, visibility.

## Process layout

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

### Shards

A shard is a thread pinned to one core that runs one MsQuic execution context: `ExecutionCreate`, then `ExecutionPoll` plus `epoll_wait` on the same queue. The shard's wakeup eventfd sits in that queue too, so MsQuic I/O, ring consumption and dirty-set flushes all happen on one thread with no cross-thread hops.

- **Connection ownership.** MsQuic invokes a connection's callbacks on the worker that owns its partition. The listener callback runs there too, so the shard that accepts a connection owns its state.
- **Migration.** Client migration is disabled, which keeps the 4-tuple, and so the partition, stable. If MsQuic still moves a connection, its events are forwarded to the home shard's mailbox in order (`Conn::moved`).

### Replica

The replica is the single writer. It owns `Record`s, the uuid→handle index and, per **partition**, a counted B+tree for each sort (`core/counted_btree.hpp`). A partition is one (version bucket | all, password filter, region | all) combination.

- A visible entry sits in at most 8 partitions × 3 sorts = 24 trees.
- A **view** is (partition, sort, window K). Window membership changes are computed with order statistics, in O(log n) per affected view:

| transition | patches emitted |
|---|---|
| in → in | partial patch (changed fields only) |
| out → in | full entry, plus removal of the entry pushed to rank K |
| in → out | removal, plus full entry of the one pulled up to rank K−1 |

Each mutation produces at most one encoded `Delta` frame per affected view, shared by every subscriber of that view on every shard.

Published state for other threads is immutable. Each change creates a new `PubEntry`, swaps it into a handle-indexed atomic table and retires the old one. Ring cursors double as quiescent-state counters: a retired object is freed once every consumer has passed the event that retired it.

Requests that must be ordered with mutations (subscribe, which needs snapshot plus registration at the exact ring position) travel back on the ring. Text searches go to search workers, which keep their own trigram index fed from the same ring.

### Fan-out, step by step

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
4. Lost datagrams (`LOST_SUSPECT` / `LOST_DISCARDED`) re-mark their entries dirty, so repairs always carry current values. Patches carry per-field sequence numbers, so reordering and duplication are harmless (see PROTOCOL.md).
5. When a busy connection's dirty set for a view grows past 2 × window (window churn produces a stream of entries leaving), the shard drops every pending removal and owes the client one `WindowSync`: the handles currently in the window, about 3 bytes each. The client removes everything not listed, and the remaining dirty entries (all window members) carry the new values. Memory per slow client is bounded by its windows, and catching up costs a few hundred bytes instead of a snapshot. (An earlier design resent a snapshot here; the load test showed it turning congestion into a snapshot storm.)

## Replication

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

## Security

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

## C++26 usage

| feature | where | fallback |
|---|---|---|
| P2996 static reflection | `wire/codec.hpp` generates the protobuf codec from plain structs | Boost.PFR (identical output, checked by golden vectors) |
| P2900 contracts | `SB_PRE` / `SB_ASSERT` | assert-style macros |
| `std::inplace_vector` | small fixed-capacity sets (frame items, subscriptions) | `boost::container::static_vector` |
| `std::atomic::wait`, `std::jthread`, `std::stop_token` | wakeups and thread lifecycle | — |

CMake probes each feature (`cmake/SbFeatures.cmake`), and `std::execution` is not used. The hot path is a hand-written actor pipeline (MPSC inbox → single writer → broadcast ring), which needs no generic scheduler.

## Performance notes

- **Per mutation:** O(log n) tree updates for each of up to 24 trees, plus at most about 48 frame encodes. The replica handles tens of thousands of mutations per second on one core.
- **Per delivery:** one `DatagramSend` of a shared buffer. The kernel and crypto path, about 1.5–3 µs per packet, dominates. MsQuic's epoll datapath already batches receives with `recvmmsg` and uses UDP GSO for sends and GRO for receives; beyond that the per-core ceiling rises with NIC RSS queues and IRQ affinity (see "Datapath" in OPERATIONS.md for why XDP and io_uring are not options).
- **Memory per idle browser** is small: one `Conn`, one control stream, and a dirty set that is empty until the connection falls behind. Snapshots are shared buffers, cached per (view, vseq).
- **Handshakes:**
  - ECDSA P-256 certificates;
  - resumption tickets valid across edges;
  - Retry under memory pressure;
  - `MaxWorkerQueueDelayUs` sheds new handshakes before delivery latency suffers.
