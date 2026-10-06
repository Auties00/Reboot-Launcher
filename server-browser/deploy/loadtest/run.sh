#!/usr/bin/env bash
# Runs the load scenarios against one sb-edge on this machine and writes a Markdown report.
#   IMAGE=server-browser/sb-edge:ci deploy/loadtest/run.sh out/
# Both the edge and the load generator share the host, so client-side latency includes the load
# generator's own queueing; the report therefore also shows the edge's server-side handoff latency.
# Needs Docker and passwordless sudo (kernel tuning).
# PROFILE lists the scenarios to profile with perf during their steady state (empty: none).
set -euo pipefail

out="${1:-loadtest-out}"
image="${IMAGE:-server-browser/sb-edge:ci}"
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$out"
out="$(realpath "$out")"
cores="$(nproc)"
shards="${SHARDS:-$(( cores > 2 ? cores / 2 : 1 ))}"
profile="${PROFILE-fanout-max fanout-max-40k}"

sudo sysctl -q -w net.core.rmem_max=67108864 net.core.wmem_max=67108864 \
    net.core.rmem_default=4194304 net.core.wmem_default=4194304 \
    net.core.netdev_max_backlog=250000 net.ipv4.ip_local_port_range="10240 65535" \
    fs.file-max=4194304 fs.nr_open=4194304 >/dev/null

perf=""
flamegraph="$out/.flamegraph"
if [ -n "$profile" ]; then
    sudo apt-get install -y -qq "linux-tools-$(uname -r)" linux-tools-common >/dev/null 2>&1 || true
    perf="$(ls /usr/lib/linux-tools/*/perf 2>/dev/null | tail -1 || true)"
    if [ -n "$perf" ]; then
        sudo sysctl -q -w kernel.perf_event_paranoid=-1 kernel.kptr_restrict=0 >/dev/null
        git clone -q --depth 1 https://github.com/brendangregg/FlameGraph "$flamegraph" || true
    else
        echo "perf is not available for kernel $(uname -r); skipping profiles" >&2
    fi
fi

# Samples the edge on CPU for 20 s: call stacks are unwound from DWARF, so no frame pointers needed.
start_profile() {  # start_profile LABEL; sets the caller's perf_pid
    local pid
    pid="$(docker inspect -f '{{.State.Pid}}' sb-load-edge)"
    sudo "$perf" record -q -e cpu-clock -F 499 --call-graph dwarf,8192 -p "$pid" \
        -o "$out/$1.perf.data" -- sleep 20 > "$out/$1.perf.log" 2>&1 &
    perf_pid=$!
}

# Must run while the edge container exists: perf resolves its symbols through the container's root.
profile_report() {  # profile_report LABEL
    local data="$out/$1.perf.data"
    sudo test -s "$data" || return 0
    local rep=(sudo "$perf" report -i "$data" --stdio -q)
    {
        echo "# $1: sb-edge CPU profile (cpu-clock, 499 Hz, 20 s of steady state)"
        echo; echo "## By thread"; "${rep[@]}" --no-children --sort comm -g none 2>/dev/null | head -20
        echo; echo "## By shared object"; "${rep[@]}" --no-children --sort dso -g none 2>/dev/null | head -20
        echo; echo "## Hottest functions (self time)"
        "${rep[@]}" --no-children --sort dso,sym -g none --percent-limit 0.3 2>/dev/null | head -80
        echo; echo "## Shard threads, inclusive time"
        "${rep[@]}" --children --comms "$(printf 'sb-shard-%s,' $(seq 0 $((shards - 1))) | sed 's/,$//')" \
            --sort sym -g none --percent-limit 2 2>/dev/null | head -80
    } > "$out/$1.profile.txt"
    if [ -x "$flamegraph/flamegraph.pl" ]; then
        sudo "$perf" script -i "$data" 2>/dev/null | "$flamegraph/stackcollapse-perf.pl" > "$out/$1.folded"
        "$flamegraph/flamegraph.pl" --title "sb-edge: $1" "$out/$1.folded" > "$out/$1.flamegraph.svg"
    fi
    sudo rm -f "$data"  # hundreds of MB; the report, folded stacks and flame graph are kept
}

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
    echo "| scenario | hosts | browsers × subs | updates/s | deliveries/s | client p50 / p99 | server handoff p99 | edge CPU | loadgen CPU | deliveries per edge core-s | edge RSS | RSS per conn | autocorked | window syncs | errors |"
    echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"
} > "$report"

scenario() {  # scenario LABEL HOSTS RATE BROWSERS SUBS DURATION
    local label="$1" hosts="$2" rate="$3" browsers="$4" subs="$5" duration="$6"
    start_edge
    docker run --rm --name "sb-loadgen-$label" --network host --user "$(id -u):$(id -g)" --ulimit nofile=1048576:1048576 -v "$out:/out" \
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
    local ramp_done="" perf_pid=""
    local profiled=""
    [ -n "$perf" ] && [[ " $profile " == *" $label "* ]] && profiled=1
    # Ramp, run and teardown together never take this long; past it the load generator is stuck.
    local deadline=$(( $(date +%s) + duration + browsers / 2000 + 300 ))
    while kill -0 "$lg" 2>/dev/null; do
        if [ "$(date +%s)" -gt "$deadline" ]; then
            echo "$label: load generator did not finish, killing it" >&2
            docker kill "sb-loadgen-$label" >/dev/null 2>&1 || true
            break
        fi
        if [ -z "$ramp_done" ] && grep -q "ramp complete" "$out/$label.log" 2>/dev/null; then
            ramp_done="$(date +%s.%N)"
            curl -s http://127.0.0.1:9100/metrics > "$out/$label.ramp.metrics"
        fi
        # Profile once the load generator has been at full rate for a few seconds.
        if [ -n "$profiled" ] && [ -n "$ramp_done" ] && [ -z "$perf_pid" ] &&
            awk -v r="$ramp_done" -v n="$(date +%s.%N)" 'BEGIN { exit !(n > r + 8) }'; then
            start_profile "$label"
        fi
        sleep 1
    done
    wait "$lg" || true
    wait "$sampler" || true
    cat "$out/$label.log"
    curl -s http://127.0.0.1:9100/metrics > "$out/$label.metrics"
    docker logs sb-load-edge > "$out/$label.edge.log" 2>&1
    if [ -n "$perf_pid" ]; then
        wait "$perf_pid" || true
        profile_report "$label"
    fi
    docker rm -f sb-load-edge >/dev/null
    python3 "$here/summarize.py" "$out" "$label" "${ramp_done:-0}" "$duration" >> "$report"
}

# fanout-max-40k doubles fanout-max's browsers and nothing else: compare their profiles.
# idle-conns sends no updates, so it measures what an idle subscribed browser costs.
# label             hosts rate browsers subs seconds
scenario baseline       200  1.0     2000    2      45
scenario browse-heavy  1000  1.0    20000    2      60
scenario fanout-max      50  4.0    20000    1      60
scenario fanout-max-40k  50  4.0    40000    1      60
scenario idle-conns     100  0      40000    1      45

{
    echo
    echo "- deliveries/s: patches received by browsers. Client latency: host update → browser datagram on one"
    echo "  in-process clock, so it includes the load generator's own queueing on this shared machine."
    echo "- server handoff: replica mutation → datagram handed to MsQuic, from the edge's histogram."
    echo "- autocorked: share of datagrams built from dirty sets because the connection was still busy."
    echo "- window syncs: lagging subscriptions caught up with a membership list instead of replaying removals."
} >> "$report"
cat "$report"
