# server-browser

`server-browser` is the real-time server browser for Reboot. Hosts publish their game servers to it, and players browse and join them. Every change reaches the people looking at it as soon as it happens, with no polling and no batching tick.

It is written in C++26 on MsQuic and scales out across any number of edges, each holding the whole registry, kept in sync over NATS JetStream. It replaces the Dart WebSocket backend from the `refactor` branch. That backend broadcast every change to every client, never expired dead hosts, let anyone overwrite anyone's entry, and published password hashes along with encrypted IPs.

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
