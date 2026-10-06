# Operations

## Build

The supported toolchain is Linux with GCC 16 (or Clang 21+ for sanitizer and fuzz builds), CMake ≥ 3.30, vcpkg, and MsQuic ≥ 2.5 built against the system OpenSSL 3.5. The Dockerfile pins all of it:

```sh
cd server-browser
docker build -f deploy/docker/Dockerfile --target test .             # build plus unit and integration tests
docker build -f deploy/docker/Dockerfile -t sb-edge .                # runtime image (distroless)
```

On a Linux host with the same toolchain:

```sh
cmake --preset gcc-release && cmake --build --preset gcc-release && ctest --preset gcc-release
cmake --preset clang-asan  && cmake --build --preset clang-asan  && ctest --preset clang-asan
./build/clang-asan/fuzz/fuzz_messages -max_total_time=60
```

`SB_NATS_URL=nats://127.0.0.1:4222` enables the multi-edge tests, which need nats-server ≥ 2.11 with JetStream.

## Run

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

## Signals

| signal | effect |
|---|---|
| SIGTERM | drain: `/readyz` turns 503 → wait `drain_delay_ms` → GoAway to every client, jittered over `drain_spread_ms` → close the rest → exit |
| SIGINT | fast stop: close every connection, exit |
| SIGHUP | reload the TLS certificate for new connections |

**Rolling upgrade:** drain one edge at a time. Hosts move to other edges with their tokens, by compare-and-set and with no tombstone, so browsers never see them disappear.

## Kernel and NIC tuning

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
- **XDP:** with a supported NIC, `quic.xdp = true` moves UDP I/O to MsQuic's AF_XDP datapath for a large packets-per-second gain.

## Metrics

`GET /metrics` (Prometheus):

| metric | watch for |
|---|---|
| `sb_delivery_seconds` (histogram) | replica-to-datagram handoff; p99 should stay in single-digit milliseconds |
| `sb_connections_browser`, `sb_connections_host` | per shard; uneven shards point at RSS problems |
| `sb_datagrams_direct_total` vs `sb_datagrams_flush_total` | flushes rising means clients or the network can't keep up (autocork engaging) |
| `sb_datagrams_lost_total`, `sb_repairs_total` | network loss |
| `sb_resnapshots_total` | clients falling more than two windows behind |
| `sb_ring_stalls_total` | a shard can't keep up with the replica: add cores or edges |
| `sb_lifecycle_conflicts_total` | concurrent registrations of the same id |
| `sb_rate_limited_total`, `sb_connections_rejected_total` | abuse, or limits that are too tight |
| `msquic_*` | handshake failures, connection queue depth (queue delay sheds handshakes) |

Alert on `/readyz` failures, delivery p99 > 50 ms for 5 minutes, ring stalls > 0, and backbone disconnects (in the logs).

## Capacity planning

Measure with `sb-loadgen` against `deploy/config/loadtest.toml` from separate machines, spreading source addresses with `--bind`:

```sh
sb-loadgen -s edge:4433 --hosts 20000 --update-rate 0.5 --browsers 200000 --subs 2 --window 50 \
           --ramp 5000 --duration 600 --sample 0.01 --bind 10.0.0.11 --bind 10.0.0.12 ...
```

`sb-loadgen` prints delivery latency (host update → browser datagram) percentiles every report interval, along with throughput. Size the fleet by deliveries per second per edge, not by connections:

```
deliveries/s ≈ mutations/s on hot views × subscribers of those views
```

Scale out by adding edges. Every edge holds the full registry, so edges are interchangeable.

**Keep browsers honest:** the launcher should connect only while the server browser is visible, or while hosting.
