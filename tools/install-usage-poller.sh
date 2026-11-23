#!/usr/bin/env bash
# Installs the Claude usage poller as an always-on service on a Linux server (systemd).
#
#   curl -fsSLO https://github.com/dethdonald/Desk.Display./releases/latest/download/install-usage-poller.sh
#   bash install-usage-poller.sh http://DISPLAY-IP DISPLAY_TOKEN
#
# Needs: python3, a Claude Code login on this machine for the same user (run `claude` once),
# and sudo for creating the service. Re-run any time to change the address or token.
set -euo pipefail
DISPLAY_URL="${1:-}"; DISPLAY_TOKEN="${2:-}"
if [ -z "$DISPLAY_URL" ] || [ -z "$DISPLAY_TOKEN" ]; then
  echo "usage: bash install-usage-poller.sh http://DISPLAY-IP DISPLAY_TOKEN"; exit 1
fi
case "$DISPLAY_URL" in http*) ;; *) DISPLAY_URL="http://$DISPLAY_URL";; esac
DISPLAY_URL="${DISPLAY_URL%/}"; DISPLAY_URL="${DISPLAY_URL%/api/usage}/api/usage"

RUN_USER="${SUDO_USER:-$USER}"
RUN_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"
CREDS="$RUN_HOME/.claude/.credentials.json"
if [ ! -f "$CREDS" ]; then
  echo "No Claude login found at $CREDS."
  echo "Run 'claude' as $RUN_USER, sign in with your Claude account, then run this again."; exit 1
fi

DEST=/opt/deskdisplay
sudo mkdir -p "$DEST"
URL="https://github.com/dethdonald/Desk.Display./releases/latest/download/claude-usage-poller.py"
SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "$SRC_DIR/claude-usage-poller.py" ]; then sudo cp "$SRC_DIR/claude-usage-poller.py" "$DEST/"
else sudo curl -fsSL "$URL" -o "$DEST/claude-usage-poller.py"; fi
sudo chmod 755 "$DEST/claude-usage-poller.py"

sudo install -m 600 -o "$RUN_USER" /dev/null /etc/deskdisplay-usage.env
printf 'DESKDISPLAY_URL=%s\nDESKDISPLAY_TOKEN=%s\nPOLL_INTERVAL=180\n' "$DISPLAY_URL" "$DISPLAY_TOKEN" | sudo tee /etc/deskdisplay-usage.env >/dev/null

sudo tee /etc/systemd/system/deskdisplay-usage.service >/dev/null <<EOF
[Unit]
Description=Desk Display - Claude usage poller
After=network-online.target
Wants=network-online.target

[Service]
User=$RUN_USER
Environment=HOME=$RUN_HOME
EnvironmentFile=/etc/deskdisplay-usage.env
ExecStart=/usr/bin/python3 $DEST/claude-usage-poller.py
Restart=always
RestartSec=30

[Install]
WantedBy=multi-user.target
EOF

echo "Testing once..."
sudo -u "$RUN_USER" env HOME="$RUN_HOME" DESKDISPLAY_URL="$DISPLAY_URL" DESKDISPLAY_TOKEN="$DISPLAY_TOKEN" \
  /usr/bin/python3 "$DEST/claude-usage-poller.py" --once || true

sudo systemctl daemon-reload
sudo systemctl enable --now deskdisplay-usage.service
sudo systemctl restart deskdisplay-usage.service
echo
echo "Installed. It updates the display every 3 minutes."
echo "Live log:  journalctl -u deskdisplay-usage -f"
