#!/usr/bin/env bash
# Runs the load scenarios against one sb-edge on this machine and writes a Markdown report.
#   IMAGE=server-browser/sb-edge:ci deploy/loadtest/run.sh out/
# Both the edge and the load generator share the host, so the numbers are a lower bound for a
# dedicated edge machine. Needs Docker and passwordless sudo (kernel tuning).
set -euo pipefail

out="${1:-loadtest-out}"
image="${IMAGE:-server-browser/sb-edge:ci}"
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$out"
cores="$(nproc)"
shards="${SHARDS:-$(( cores > 2 ? cores / 2 : 1 ))}"

sudo sysctl -q -w net.core.rmem_max=67108864 net.core.wmem_max=67108864 \
    net.core.rmem_default=4194304 net.core.wmem_default=4194304 \
    net.core.netdev_max_backlog=250000 net.ipv4.ip_local_port_range="10240 65535" \
    fs.file-max=4194304 fs.nr_open=4194304 >/dev/null

# Source addresses on loopback so each one has its own ephemeral port range.
binds=()
for i in $(seq 2 17); do binds+=(--bind "127.0.0.$i"); done

metric() {  # metric NAME -> sum of samples
    curl -s http://127.0.0.1:9100/metrics | awk -v n="$1" '$1 == n || index($1, n "{") == 1 { s += $2 } END { printf "%.0f", s }'
}

start_edge() {
    docker rm -f sb-load-edge >/dev/null 2>&1 || true
    docker run -d --name sb-load-edge --network host --ulimit nofile=1048576:1048576 \
        -v "$here/../config/loadtest.toml:/etc/sb/edge.toml:ro" "$image" \
        --config /etc/sb/edge.toml --shards "$shards" >/dev/null
    for _ in $(seq 1 100); do
        curl -sf http://127.0.0.1:9100/readyz >/dev/null && return 0
        sleep 0.2
    done
    docker logs sb-load-edge
    return 1
}

report="$out/report.md"
{
    echo "## sb-edge load test"
    echo
    echo "Machine: $cores vCPU, $(awk '/MemTotal/ { printf "%.1f GiB", $2 / 1048576 }' /proc/meminfo); edge shards: $shards;"
    echo "edge and load generator share the machine."
    echo
    echo "| scenario | hosts | browsers × subs | updates/s | deliveries/s | p50 | p99 | p99.9 | edge CPU | edge RSS | autocorked | errors |"
    echo "|---|---|---|---|---|---|---|---|---|---|---|---|"
} > "$report"

scenario() {  # scenario LABEL HOSTS RATE BROWSERS SUBS DURATION
    local label="$1" hosts="$2" rate="$3" browsers="$4" subs="$5" duration="$6"
    start_edge
    local cpu0 t0 cpu1 t1 rss direct flush
    cpu0="$(metric sb_process_cpu_seconds_total)"
    t0="$(date +%s.%N)"
    docker run --rm --network host --user "$(id -u):$(id -g)" --ulimit nofile=1048576:1048576 -v "$(realpath "$out"):/out" \
        --entrypoint /usr/local/bin/sb-loadgen "$image" \
        -s 127.0.0.1:4433 --hosts "$hosts" --update-rate "$rate" --browsers "$browsers" --subs "$subs" \
        --window 50 --duration "$duration" --ramp 4000 --sample 0.05 --report 10 \
        --label "$label" --summary "/out/$label.json" "${binds[@]}" | tee "$out/$label.log" || true
    cpu1="$(metric sb_process_cpu_seconds_total)"
    t1="$(date +%s.%N)"
    rss="$(metric sb_process_resident_bytes)"
    direct="$(metric sb_datagrams_direct_total)"
    flush="$(metric sb_datagrams_flush_total)"
    curl -s http://127.0.0.1:9100/metrics > "$out/$label.metrics"
    docker logs sb-load-edge > "$out/$label.edge.log" 2>&1
    docker rm -f sb-load-edge >/dev/null
    python3 - "$out/$label.json" "$cpu0" "$cpu1" "$t0" "$t1" "$rss" "$direct" "$flush" >> "$report" <<'PY'
import json, sys
s = json.load(open(sys.argv[1]))
cpu = (float(sys.argv[3]) - float(sys.argv[2])) / (float(sys.argv[5]) - float(sys.argv[4]))
rss, direct, flush = (int(float(x)) for x in sys.argv[6:9])
lat = s["latency_us"]
ms = lambda us: f"{us / 1000:.2f} ms"
corked = flush / max(1, direct + flush)
print(f"| {s['label']} | {s['hosts']} | {s['browsers']} × {s['subs']} | {s['updates_per_s']:.0f} | {s['patches_per_s']:.0f} | "
      f"{ms(lat['p50'])} | {ms(lat['p99'])} | {ms(lat['p999'])} | {cpu:.2f} cores | {rss / 2**20:.0f} MiB | {corked:.1%} | {s['errors']} |")
PY
}

# label             hosts rate browsers subs seconds
scenario baseline       200  1.0     2000    2      45
scenario browse-heavy  1000  1.0    20000    2      60
scenario fanout-max      50  4.0    20000    1      60
scenario idle-conns     100  0.2    40000    1      45

{
    echo
    echo "deliveries/s counts patches received by browsers; latency is host update → browser datagram,"
    echo "measured by sampled browsers with one in-process clock. 'autocorked' is the share of datagrams"
    echo "built from dirty sets because a connection was still busy (coalescing under load)."
} >> "$report"
cat "$report"
