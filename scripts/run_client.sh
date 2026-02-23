#!/usr/bin/env bash
# Run P2P client (run after peer). Connects to signaling and coturn. With xquic build, acts as MOQ Subscriber.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${PROJECT_ROOT}/build/bin/client"
SIGNALING="${1:-localhost:8888}"
COTURN_HOST="${2:-127.0.0.1}"
if [[ ! -x "$BIN" ]]; then
  echo "Build client first: cmake -B build && cmake --build build --target quic-p2p-client"
  exit 1
fi
echo "[client] Signaling=$SIGNALING Turn=$COTURN_HOST"
exec "$BIN" --signaling "$SIGNALING" --turn-host "$COTURN_HOST" --turn-user juice_demo --turn-pass juice_password
