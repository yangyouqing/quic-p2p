#!/usr/bin/env bash
# Run coturn for local P2P (STUN/TURN). Use scripts/coturn.conf.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONF="${SCRIPT_DIR}/coturn.conf"
if [[ ! -f "$CONF" ]]; then
  echo "Missing $CONF"
  exit 1
fi
echo "[coturn] Starting on 127.0.0.1:3478 (config: $CONF)"
exec turnserver -c "$CONF"
