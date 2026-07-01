#!/bin/bash
# Logs the monitor's resident memory (RSS, in kB) once a minute, but only while
# the process is alive. Output: rss_log.txt (epoch_seconds  rss_kB).
# Start this before the capture; it idles until the capture begins.
while true; do
    PID=$(pgrep -f firehose_monitor)
    if [ -n "$PID" ]; then
        echo "$(date +%s) $(awk '/VmRSS/{print $2}' /proc/$PID/status 2>/dev/null)" >> "$HOME/firehose/rss_log.txt"
    fi
    sleep 60
done
