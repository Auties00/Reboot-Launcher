# rbsb/1: server browser protocol

This is the contract between `sb-edge` and its clients (the launcher's browser and its game-server hosts). The message schema is [`proto/rbsb.proto`](../proto/rbsb.proto). Any protobuf runtime can decode it. Byte-exact examples live in [`tests/vectors`](../tests/vectors). `src/client/` is the C++ reference client, and `src/client/view_mirror.hpp` implements the delta rules described below.

## Transport

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

## Framing

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

## Session

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

## Browsing

### Views

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

### Subscriptions: live top-K windows

`Subscribe{req_id, sub_id, view, window}` asks for the first `window` entries of a view. The server rounds `window` up to a supported size (50 or 200) and answers with `SubOpen{req_id, sub_id, view_id, window}`. It then sends a `Snapshot{view_id, vseq, total, entries}` on a new snapshot stream, and from then on `Delta{view_id, patches}` for every change to that window.

- `view_id` names the server-side view. Several subscriptions (even from other clients) can share one; a client keeps one mirror per `view_id`.
- `total` is the number of entries in the whole view, not just the window.
- A browser that needs "every version I have installed" subscribes to several single-version views and merges them locally.
- `Unsubscribe{sub_id}` stops a subscription.

There is no batching interval. A change is sent as soon as it happens.

### Applying deltas

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

### Queries: deep pages and search

`Query{req_id, view, text, limit, cursor}` returns one page as `QueryResult{entries, next_cursor, total}`.

- Without `text`, it pages through the view in its sort order.
- With `text`, it returns entries of the view whose case-folded name, author or id contains `text` (1 to 64 bytes).
- `cursor` is opaque: pass back `next_cursor` to get the next page. An empty `next_cursor` means the end.
- Queries are not live. Re-run them, or subscribe for live data.
- `limit` is capped by `Welcome.limits.max_query_limit`; 0 means 50.
- Rate limit: about 10 queries per second per connection, with bursts.

### Resolve: deep links

`Resolve{req_id, id}` returns `ResolveResult{details?}` with the full entry, its description and `updated_ms`. This works for hidden and offline entries too, which is how `Reboot://<uuid>` links are resolved.

### Join

`Join{req_id, id, password?}` returns `JoinGrant{address, port, ticket, expires_ms}`. This is the **only** message that ever carries a server's address.

- The address is 4 bytes for IPv4 or 16 for IPv6, network order.
- If the entry is password-protected and the password is missing or wrong, the server answers `Error{WRONG_PASSWORD}`.
- Attempts are limited to about 5 per minute per (client address, entry).
- `ticket` is an HMAC over (id, address, port, expiry). It is reserved for game servers that want to admit only clients that went through the browser.

## Hosting

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

## Errors

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

## Client checklist

- Enable QUIC datagram reception, allow at least 8 server-opened unidirectional streams, and set the ALPN to `rbsb/1`.
- Validate the server certificate.
- Keep one control stream. Assign `req_id` values and match responses by them.
- Buffer deltas until a view's snapshot arrives. Apply window syncs, then patches, with per-field sequences. Accept a fresh snapshot at any time.
- Connect only while the browser screen is open, or while hosting. Idle connections cost the server memory and buy nothing.
- On `GoAway` or a lost connection, reconnect with exponential backoff and jitter, then resubscribe. Hosts re-register with their token.
- Hosts: persist `(id, token)`, heartbeat over datagrams, and push player counts as they change.
