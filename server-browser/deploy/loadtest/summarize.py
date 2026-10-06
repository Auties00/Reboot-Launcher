"""One Markdown table row for a load scenario: loadgen JSON summary + edge metrics and samples."""
import json
import re
import sys

out, label, ramp_done, duration = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])


def metrics(path):
    sums, buckets = {}, []
    try:
        lines = open(path).read().splitlines()
    except OSError:
        return sums, buckets
    for line in lines:
        if line.startswith('sb_delivery_seconds_bucket'):
            le = re.search(r'le="([^"]+)"', line).group(1)
            buckets.append((float('inf') if le == '+Inf' else float(le), float(line.split()[-1])))
            continue
        m = re.match(r'([a-z_]+)(\{[^}]*\})? ([0-9.e+]+)$', line)
        if m:
            sums[m.group(1)] = sums.get(m.group(1), 0.0) + float(m.group(3))
    return sums, buckets


def delta_percentile(before, after, p):
    # Histogram difference between two scrapes (cumulative buckets).
    diff = [(le, a - (before[i][1] if i < len(before) else 0)) for i, (le, a) in enumerate(after)]
    total = diff[-1][1] if diff else 0
    for le, v in diff:
        if total and v >= p * total:
            return le
    return float('nan')


s = json.load(open(f'{out}/{label}.json'))
ramp_m, ramp_b = metrics(f'{out}/{label}.ramp.metrics')
end_m, end_b = metrics(f'{out}/{label}.metrics')

# Steady window: loadgen waits 2 s after the ramp, then runs `duration` seconds.
start, stop = ramp_done + 2, ramp_done + 2 + duration
samples = []
for line in open(f'{out}/{label}.samples'):
    t, cpu, rss, browsers, hosts = (float(x) for x in line.split())
    if start <= t <= stop:
        samples.append((t, cpu, rss, browsers + hosts))
cpu = (samples[-1][1] - samples[0][1]) / (samples[-1][0] - samples[0][0]) if len(samples) >= 2 else float('nan')
rss = max((x[2] for x in samples), default=0)
conns = max((x[3] for x in samples), default=0)

direct = end_m.get('sb_datagrams_direct_total', 0) - ramp_m.get('sb_datagrams_direct_total', 0)
flush = end_m.get('sb_datagrams_flush_total', 0) - ramp_m.get('sb_datagrams_flush_total', 0)
syncs = end_m.get('sb_window_syncs_total', 0) - ramp_m.get('sb_window_syncs_total', 0)
handoff = delta_percentile(ramp_b, end_b, 0.99)

lat = s['latency_us']
ms = lambda us: f'{us / 1000:.1f}'
per_core = s['patches_per_s'] / cpu if cpu == cpu and cpu > 0 else float('nan')
print(f"| {label} | {s['hosts']} | {s['browsers']} × {s['subs']} | {s['updates_per_s']:.0f} | {s['patches_per_s']:,.0f} | "
      f"{ms(lat['p50'])} / {ms(lat['p99'])} ms | ≤ {handoff * 1000:.1f} ms | {cpu:.2f} cores | {s.get('loadgen_cpu_cores', 0):.2f} cores | {per_core:,.0f} | "
      f"{rss / 2**20:,.0f} MiB | {rss / conns / 1024 if conns else 0:.0f} KiB ({conns:,.0f}) | "
      f"{flush / max(1, direct + flush):.1%} | {syncs:,.0f} | {s['errors']} |")
