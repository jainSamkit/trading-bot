#!/usr/bin/env python3
"""
Plot tick-to-trade latency percentiles from an Influx Annotated CSV snapshot.

Usage:
    python3 scripts/plot_latency.py ~/Desktop/latency_snapshot_15min.csv

Produces two PNGs next to the input CSV:
    <stem>_per_stage.png   — grouped bar chart, per-stage p50/p90/p99
    <stem>_log.png         — log-scale stacked view; the CV-grade chart

Requires: pandas, matplotlib (both already in .venv if you have one).
"""

import sys
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np


# ── CLI ──────────────────────────────────────────────────────────────────────
if len(sys.argv) < 2:
    print("usage: plot_latency.py <influx_annotated.csv>", file=sys.stderr)
    sys.exit(1)
path = Path(sys.argv[1])
if not path.exists():
    print(f"file not found: {path}", file=sys.stderr)
    sys.exit(1)


# ── Read Influx annotated CSV ────────────────────────────────────────────────
# Lines starting with '#' are metadata (group/datatype/default). pandas's
# comment='#' would drop them, but the data header line (",result,table,...")
# starts with a comma and would also become a "comment". So: skip the first
# three '#'-prefixed lines explicitly, then read the rest.
with path.open() as f:
    lines = [ln for ln in f if not ln.startswith("#")]
df = pd.read_csv(pd.io.common.StringIO("".join(lines)))

# Drop Influx's internal columns; keep only what we plot.
keep = [c for c in ["event", "target", "msg", "_field", "_value"] if c in df.columns]
df = df[keep].copy()
df["_value"] = pd.to_numeric(df["_value"], errors="coerce")
df = df.dropna(subset=["_value"])


# ── Pick the headline events; collapse msg variants by taking the worst ─────
HEADLINE = ["ws_read", "ws_frame", "json_parse", "handler",
            "queue_time", "tick_to_trade", "wire_out"]

df = df[df["event"].isin(HEADLINE)]

# Build a label per (event, target) — keeps queue_time/strategy etc. distinct.
df["target"] = df["target"].fillna("")
df["label"] = df.apply(
    lambda r: f"{r['event']}/{r['target']}" if r["target"] else r["event"],
    axis=1,
)

# For events split across msg types (json_parse/handler), take the WORST
# percentile across msgs — that's what the "stage p99" actually represents.
agg = (
    df.groupby(["label", "_field"])["_value"]
      .max()
      .unstack("_field")
      .reindex(columns=["p50", "p90", "p99"])
)

# Order rows by p50 ascending so the chart reads left→right as fast→slow.
agg = agg.sort_values("p50")

# Pull tick_to_trade out and append at the right so it visually anchors the chart.
if "tick_to_trade" in agg.index:
    ttt = agg.loc[["tick_to_trade"]]
    agg = agg.drop("tick_to_trade")
    agg = pd.concat([agg, ttt])


# ── Plot 1: grouped bar, linear µs ───────────────────────────────────────────
fig, ax = plt.subplots(figsize=(12, 5))
x = np.arange(len(agg))
w = 0.27
for i, pct in enumerate(["p50", "p90", "p99"]):
    if pct not in agg.columns:
        continue
    ax.bar(x + (i - 1) * w, agg[pct] / 1000.0, w, label=pct)
ax.set_xticks(x)
ax.set_xticklabels(agg.index, rotation=30, ha="right")
ax.set_ylabel("Latency (µs)")
ax.set_title(
    "Per-stage latency — AWS Tokyo c7gn.2xlarge (Graviton 3E)\n"
    "isolcpus + nohz_full + rcu_nocbs on cores 2-5, "
    "SCHED_FIFO 50, pinned cores, 2MB hugepages, mlockall"
)
ax.legend()
ax.grid(axis="y", alpha=0.3)
plt.tight_layout()
out1 = path.with_name(f"{path.stem}_per_stage.png")
fig.savefig(out1, dpi=150)
print(f"wrote {out1}")


# ── Plot 2: log-scale (the CV chart — readable across 3 orders of magnitude) ─
fig, ax = plt.subplots(figsize=(12, 5))
for i, pct in enumerate(["p50", "p90", "p99"]):
    if pct not in agg.columns:
        continue
    ax.bar(x + (i - 1) * w, agg[pct], w, label=pct)
ax.set_xticks(x)
ax.set_xticklabels(agg.index, rotation=30, ha="right")
ax.set_ylabel("Latency (ns) — log scale")
ax.set_yscale("log")
ax.set_title(
    "Per-stage latency (log scale) — AWS Tokyo c7gn.2xlarge\n"
    "tick-to-trade p50 = "
    f"{agg.loc['tick_to_trade', 'p50']:.0f} ns "
    f"({agg.loc['tick_to_trade', 'p50']/1000:.2f} µs)   "
    f"p99 = {agg.loc['tick_to_trade', 'p99']:.0f} ns "
    f"({agg.loc['tick_to_trade', 'p99']/1000:.2f} µs)"
)
ax.legend()
ax.grid(axis="y", which="both", alpha=0.3)
plt.tight_layout()
out2 = path.with_name(f"{path.stem}_log.png")
fig.savefig(out2, dpi=150)
print(f"wrote {out2}")


# ── Also print the table to stdout for the CV bullet ────────────────────────
print()
print(agg.fillna(0).astype(int).to_string())
