# Real-Time Firehose Telemetry Monitor

A multithreaded real-time embedded application (C, POSIX threads) that subscribes
to the Bluesky **Jetstream** firehose over a secure WebSocket, classifies incoming
events, and logs per-second telemetry to CSV on a **Raspberry Pi Zero W**.

Coursework: *Real-Time Embedded Systems, 2026*. The included `metrics_log.txt` is a
continuous **24-hour capture on physical hardware** (Raspberry Pi Zero W, ARMv6,
Raspberry Pi OS Lite / Debian Trixie) for **2026-06-30 00:00:00–23:59:59 EEST**.

## Architecture

Producer–consumer with three POSIX threads sharing one bounded circular buffer:

- **Producer** (`producer.c`) — event-driven libwebsockets client. Each JSON frame is
  reassembled and pushed into the ring buffer (non-blocking, *drop-newest* on
  overflow) and control returns immediately to the network loop. Automatic
  reconnection with exponential backoff, plus PING keepalive / hangup dead-link
  detection.
- **Consumer** (`consumer.c`) — blocks on the ring buffer, parses each frame with
  cJSON, reads the `kind` field, and increments mutex-protected counters
  (`commit`, `identity`, `account`, `info`). No printing or file I/O in this thread.
- **Monitor** (`monitor.c`) — fires once per second on a **drift-free** schedule
  (`clock_nanosleep` + `TIMER_ABSTIME` on `CLOCK_MONOTONIC`), snapshots and zeroes
  the counters, reads CPU% from `/proc/stat`, computes buffer occupancy, and appends
  one CSV row to `metrics_log.txt` (`CLOCK_REALTIME` timestamp).

Two separate mutexes (buffer vs. counters) minimise contention. Shutdown is
signal-driven: `main` blocks `SIGINT`/`SIGTERM` and waits in `sigwait`, so the worker
threads are never interrupted.

## Files

- `config.h` — tunables (endpoint, buffer geometry, log path)
- `ring_buffer.[ch]` — bounded circular queue + synchronisation
- `producer.[ch]` — WebSocket client + reconnection
- `consumer.[ch]` — JSON parse + `kind` classification
- `counters.[ch]` — mutex-protected counters
- `monitor.[ch]` — periodic timer, CPU%, CSV logging
- `main.c` — thread setup + signal-driven shutdown
- `Makefile` — native build for the Pi
- `metrics_log.txt` — **24-hour capture (2026-06-30)**
- `rss_log.txt` — process RSS sampled once/min (memory-stability evidence)
- `post_process.py` — regenerates the analysis plots
- `plots/` — generated figures
- `deploy/` — systemd units + RSS logger used for the unattended run

## Build (on the Raspberry Pi)

```
sudo apt install -y build-essential libwebsockets-dev libcjson-dev
make
```

## Run

```
./firehose_monitor      # Ctrl-C (or SIGTERM) to stop
```

CSV columns:
`Seconds,Nanoseconds,Commit_Count,Identity_Count,Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct`

## Reproduce the plots

```
pip install pandas numpy matplotlib
python3 post_process.py metrics_log.txt rss_log.txt
```

## Key results (24 h on the Pi Zero W)

- 4.0 M messages processed; mean 46 Hz, peak 599 Hz
- Timer drift over 24 h ≈ 0 ms; jitter 97.6 % within ±1 ms
- Peak buffer occupancy 0.2 %; **zero dropped frames**
- Mean CPU 11.7 % of one ARMv6 core (r = 0.70 with message rate)
- No memory leaks (valgrind on an x86 build); RSS plateaus on hardware
- Survives network outages: automatic reconnection verified
