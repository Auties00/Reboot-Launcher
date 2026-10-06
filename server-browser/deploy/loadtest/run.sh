#!/usr/bin/env bash
# Runs the load scenarios against one sb-edge on this machine and writes a Markdown report.
#   IMAGE=server-browser/sb-edge:ci deploy/loadtest/run.sh out/
# Both the edge and the load generator share the host, so client-side latency includes the load
# generator's own queueing; the report therefore also shows the edge's server-side handoff latency.
# Needs Docker and passwordless sudo (kernel tuning).
set -euo pipefail

out="${1:-loadtest-out}"
image="${IMAGE:-server-browser/sb-edge:ci}"
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$out"
out="$(realpath "$out")"
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
    echo "edge and load generator share the machine. Edge figures are sampled during the steady state."
    echo
    echo "| scenario | hosts | browsers × subs | updates/s | deliveries/s | client p50 / p99 | server handoff p99 | edge CPU | deliveries per core-s | edge RSS | RSS per conn | autocorked | resnapshots | errors |"
    echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"
} > "$report"

scenario() {  # scenario LABEL HOSTS RATE BROWSERS SUBS DURATION
    local label="$1" hosts="$2" rate="$3" browsers="$4" subs="$5" duration="$6"
    start_edge
    docker run --rm --network host --user "$(id -u):$(id -g)" --ulimit nofile=1048576:1048576 -v "$out:/out" \
        --entrypoint /usr/local/bin/sb-loadgen "$image" \
        -s 127.0.0.1:4433 --hosts "$hosts" --update-rate "$rate" --browsers "$browsers" --subs "$subs" \
        --window 50 --duration "$duration" --ramp 2000 --sample 0.05 --report 10 \
        --label "$label" --summary "/out/$label.json" "${binds[@]}" > "$out/$label.log" 2>&1 &
    local lg=$!
    # Edge samples while the load generator runs: time, cpu seconds, rss, browsers, hosts.
    (
        while kill -0 "$lg" 2>/dev/null; do
            echo "$(date +%s.%N) $(metric sb_process_cpu_seconds_total) $(metric sb_process_resident_bytes) $(metric sb_connections_browser) $(metric sb_connections_host)"
            sleep 2
        done
    ) > "$out/$label.samples" &
    local sampler=$!
    local ramp_done=""
    while kill -0 "$lg" 2>/dev/null; do
        if [ -z "$ramp_done" ] && grep -q "ramp complete" "$out/$label.log" 2>/dev/null; then
            ramp_done="$(date +%s.%N)"
            curl -s http://127.0.0.1:9100/metrics > "$out/$label.ramp.metrics"
        fi
        sleep 1
    done
    wait "$lg" || true
    wait "$sampler" || true
    cat "$out/$label.log"
    curl -s http://127.0.0.1:9100/metrics > "$out/$label.metrics"
    docker logs sb-load-edge > "$out/$label.edge.log" 2>&1
    docker rm -f sb-load-edge >/dev/null
    python3 "$here/summarize.py" "$out" "$label" "${ramp_done:-0}" "$duration" >> "$report"
}

# label             hosts rate browsers subs seconds
scenario baseline       200  1.0     2000    2      45
scenario browse-heavy  1000  1.0    20000    2      60
scenario fanout-max      50  4.0    20000    1      60
scenario idle-conns     100  0.2    40000    1      45

{
    echo
    echo "- deliveries/s: patches received by browsers. Client latency: host update → browser datagram on one"
    echo "  in-process clock, so it includes the load generator's own queueing on this shared machine."
    echo "- server handoff: replica mutation → datagram handed to MsQuic, from the edge's histogram."
    echo "- autocorked: share of datagrams built from dirty sets because the connection was still busy."
} >> "$report"
cat "$report"
