#!/usr/bin/env python3
"""Post-processing for the firehose 24 h capture.

Loads metrics_log.txt, trims to the exact 2026-06-30 00:00:00-23:59:59 (EEST)
window, and produces the three plots required by the brief plus an RSS plot:

  1. jitter.png            - periodic-thread jitter (ms) vs time
  2. load_buffer.png       - message rate (Hz) + buffer occupancy (%), dual axis
  3. cpu_correlation.png   - CPU busy (%) vs message rate (Hz), with linear fit
  4. rss.png               - process RSS (MB) vs time (memory-stability evidence)

It also prints a numeric summary used in the report.
"""
import sys
from datetime import datetime
from zoneinfo import ZoneInfo

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

TZ = ZoneInfo("Europe/Athens")
DAY = datetime(2026, 6, 30, 0, 0, 0, tzinfo=TZ)
S0 = int(DAY.timestamp())            # 2026-06-30 00:00:00 EEST
S1 = S0 + 86400                      # exclusive end -> exactly 86400 rows

metrics = sys.argv[1] if len(sys.argv) > 1 else "metrics_log.txt"
rss_path = sys.argv[2] if len(sys.argv) > 2 else "rss_log.txt"

# ---- load -----------------------------------------------------------------
df = pd.read_csv(metrics)
df["t"] = df["Seconds"] + df["Nanoseconds"] / 1e9
df = df[(df["Seconds"] >= S0) & (df["Seconds"] < S1)].reset_index(drop=True)

df["rate"]  = df["Commit_Count"] + df["Identity_Count"] + df["Account_Count"] + df["Info_Count"]
df["hours"] = (df["t"] - df["t"].iloc[0]) / 3600.0
df["jitter_ms"] = (df["t"].diff() - 1.0) * 1000.0   # first row -> NaN

# ---- numeric summary ------------------------------------------------------
j = df["jitter_ms"].dropna()
span = df["t"].iloc[-1] - df["t"].iloc[0]
ideal = len(df) - 1
drift_ms = (span - ideal) * 1000.0

# zero-commit runs (network gaps)
zero = (df["Commit_Count"] == 0).values
runs, cur = [], 0
for z in zero:
    cur = cur + 1 if z else 0
    if not z and cur:
        pass
# proper run detection
runs = []
cur = 0
for z in zero:
    if z:
        cur += 1
    elif cur:
        runs.append(cur); cur = 0
if cur:
    runs.append(cur)

print(f"rows in window           : {len(df)}  (expect 86400)")
print(f"window start (local)     : {datetime.fromtimestamp(df['Seconds'].iloc[0], TZ)}")
print(f"window end   (local)     : {datetime.fromtimestamp(df['Seconds'].iloc[-1], TZ)}")
print(f"total messages           : {int(df['rate'].sum()):,}")
print(f"  commit/identity/account/info: "
      f"{int(df.Commit_Count.sum()):,} / {int(df.Identity_Count.sum())} / "
      f"{int(df.Account_Count.sum())} / {int(df.Info_Count.sum())}")
print(f"mean / peak rate (Hz)    : {df['rate'].mean():.1f} / {int(df['rate'].max())}")
print(f"max buffer occupancy (%) : {df['Buffer_Occupancy_Pct'].max():.3f}")
print(f"mean / peak CPU (%)      : {df['CPU_Pct'].mean():.2f} / {df['CPU_Pct'].max():.2f}")
print(f"jitter mean / std (ms)   : {j.mean():+.4f} / {j.std():.4f}")
print(f"jitter min / max (ms)    : {j.min():+.3f} / {j.max():+.3f}")
print(f"jitter within +/-1 ms    : {100.0*(j.abs() <= 1.0).mean():.3f} %")
print(f"total drift over 24 h    : {drift_ms:+.1f} ms")
print(f"zero-commit seconds      : {int(zero.sum())}  in {len(runs)} run(s); "
      f"longest run = {max(runs) if runs else 0} s")

r = np.corrcoef(df["rate"], df["CPU_Pct"])[0, 1]
print(f"Pearson r (rate vs CPU)  : {r:.3f}")

# ---- 1. jitter ------------------------------------------------------------
fig, ax = plt.subplots(figsize=(11, 4.5))
ax.scatter(df["hours"], df["jitter_ms"], s=1.2, alpha=0.25,
           color="#1f6f8b", linewidths=0)
ax.axhline(0, color="#444", lw=0.8)
ax.set_xlim(0, 24)
ax.set_xlabel("Time into 2026-06-30 (hours)")
ax.set_ylabel("Jitter (ms)")
ax.set_title("Monitor thread jitter — deviation of each 1 s tick from the ideal deadline")
ax.text(0.012, 0.97,
        f"mean {j.mean():+.4f} ms   std {j.std():.4f} ms\n"
        f"max |jitter| {j.abs().max():.3f} ms   drift over 24 h {drift_ms:+.1f} ms",
        transform=ax.transAxes, va="top", ha="left", fontsize=9,
        bbox=dict(boxstyle="round", fc="white", ec="#ccc"))
ax.grid(True, alpha=0.3)
fig.tight_layout(); fig.savefig("jitter.png", dpi=150); plt.close(fig)

# ---- 2. load + buffer -----------------------------------------------------
fig, ax1 = plt.subplots(figsize=(11, 4.5))
ax1.plot(df["hours"], df["rate"], lw=0.4, alpha=0.35, color="#1f6f8b",
         label="rate (per-second)")
roll = df["rate"].rolling(60, center=True, min_periods=1).mean()
ax1.plot(df["hours"], roll, lw=1.8, color="#0b3954", label="rate (60 s mean)")
ax1.set_xlim(0, 24)
ax1.set_xlabel("Time into 2026-06-30 (hours)")
ax1.set_ylabel("Message rate (Hz)", color="#0b3954")
ax1.tick_params(axis="y", labelcolor="#0b3954")
ax1.grid(True, alpha=0.3)

ax2 = ax1.twinx()
ax2.scatter(df["hours"], df["Buffer_Occupancy_Pct"], s=2, alpha=0.25,
            color="#c1666b", linewidths=0, label="buffer occupancy")
ax2.set_ylabel("Buffer occupancy (%)", color="#c1666b")
ax2.tick_params(axis="y", labelcolor="#c1666b")
ax2.set_ylim(0, max(0.3, df["Buffer_Occupancy_Pct"].max() * 1.4))
ax1.set_title("Network load and buffer occupancy over 24 h "
              "(burstiness vs. how full the queue gets)")
ax1.text(0.012, 0.96,
         f"peak rate {int(df['rate'].max())} Hz   "
         f"peak buffer occupancy {df['Buffer_Occupancy_Pct'].max():.2f} %",
         transform=ax1.transAxes, va="top", fontsize=9,
         bbox=dict(boxstyle="round", fc="white", ec="#ccc"))
h1, l1 = ax1.get_legend_handles_labels()
h2, l2 = ax2.get_legend_handles_labels()
ax1.legend(h1 + h2, l1 + l2, loc="upper right", fontsize=9)
fig.tight_layout(); fig.savefig("load_buffer.png", dpi=150); plt.close(fig)

# ---- 3. CPU vs rate correlation ------------------------------------------
fig, ax = plt.subplots(figsize=(7.5, 5.5))
ax.scatter(df["rate"], df["CPU_Pct"], s=2, alpha=0.12, color="#1f6f8b",
           linewidths=0)
m, b = np.polyfit(df["rate"], df["CPU_Pct"], 1)
x_hi = min(df["rate"].max(), (100.0 - b) / m)   # stop the line at CPU=100%
xs = np.array([df["rate"].min(), x_hi])
ax.plot(xs, m * xs + b, color="#c1666b", lw=2,
        label=f"linear fit: CPU = {m:.3f}*Hz + {b:.2f}\nPearson r = {r:.3f}")
ax.set_xlabel("Incoming message rate (Hz)")
ax.set_ylabel("CPU busy (%)")
ax.set_title("CPU load vs. message rate")
ax.legend(loc="upper left", fontsize=9)
ax.grid(True, alpha=0.3)
fig.tight_layout(); fig.savefig("cpu_correlation.png", dpi=150); plt.close(fig)

# ---- 4. RSS ---------------------------------------------------------------
try:
    rss = pd.read_csv(rss_path, sep=r"\s+", header=None, names=["sec", "rss_kb"])
    rss = rss[(rss["sec"] >= S0) & (rss["sec"] < S1)]
    rh = (rss["sec"] - rss["sec"].iloc[0]) / 3600.0
    fig, ax = plt.subplots(figsize=(11, 4))
    ax.plot(rh, rss["rss_kb"] / 1024.0, lw=1.5, color="#0b3954")
    ax.set_xlim(0, 24)
    ax.set_ylim(0, max(30, rss["rss_kb"].max() / 1024.0 * 1.3))
    ax.set_xlabel("Time into 2026-06-30 (hours)")
    ax.set_ylabel("Resident memory RSS (MB)")
    ax.set_title("Process memory over 24 h — plateaus, no unbounded growth")
    ax.grid(True, alpha=0.3)
    ax.text(0.012, 0.05,
            f"start {rss['rss_kb'].iloc[0]/1024:.1f} MB   "
            f"end {rss['rss_kb'].iloc[-1]/1024:.1f} MB   "
            f"(buffer alloc = {1024*16384/1e6:.0f} MB)",
            transform=ax.transAxes, fontsize=9,
            bbox=dict(boxstyle="round", fc="white", ec="#ccc"))
    fig.tight_layout(); fig.savefig("rss.png", dpi=150); plt.close(fig)
    print("rss plot written")
except Exception as e:
    print("rss plot skipped:", e)

print("done")
