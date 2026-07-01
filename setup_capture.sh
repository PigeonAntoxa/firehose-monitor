#!/bin/bash
# Sets up systemd units to capture the firehose for all of 2026-06-30.
# Starts 1 minute before midnight and stops 1 minute after the next midnight,
# so the full 00:00:00-23:59:59 window is covered with margin (trimmed later).
set -e

USER_NAME=$(whoami)
DIR=$HOME/firehose

# --- worker service: runs the monitor, auto-restarts on crash -------------
sudo tee /etc/systemd/system/firehose.service >/dev/null << EOF
[Unit]
Description=Bluesky Jetstream firehose telemetry monitor
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$USER_NAME
WorkingDirectory=$DIR
ExecStart=$DIR/firehose_monitor
Restart=on-failure
RestartSec=2
EOF

# --- start timer: fire 1 min before midnight, June 30 ---------------------
sudo tee /etc/systemd/system/firehose-start.timer >/dev/null << 'EOF'
[Unit]
Description=Start firehose capture (1 min before midnight, June 30)

[Timer]
OnCalendar=2026-06-29 23:59:00
AccuracySec=1s
Persistent=true
Unit=firehose.service

[Install]
WantedBy=timers.target
EOF

# --- stop service + timer: stop 1 min after midnight, July 1 --------------
sudo tee /etc/systemd/system/firehose-stop.service >/dev/null << 'EOF'
[Unit]
Description=Stop firehose capture

[Service]
Type=oneshot
ExecStart=/usr/bin/systemctl stop firehose.service
EOF

sudo tee /etc/systemd/system/firehose-stop.timer >/dev/null << 'EOF'
[Unit]
Description=Stop firehose capture (1 min after midnight, July 1)

[Timer]
OnCalendar=2026-07-01 00:01:00
AccuracySec=1s
Persistent=true
Unit=firehose-stop.service

[Install]
WantedBy=timers.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now firehose-start.timer firehose-stop.timer

echo
echo "=== Armed. Next scheduled times: ==="
systemctl list-timers firehose-start.timer firehose-stop.timer --no-pager
