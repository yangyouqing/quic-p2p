#!/usr/bin/env bash
# Run P2P peer (run before client). With xquic build, uses server.crt/server.key for QUIC TLS.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${PROJECT_ROOT}/build/bin/peer"
CERT_DIR="${PROJECT_ROOT}/build/bin"
SIGNALING="${1:-localhost:8888}"
COTURN_HOST="${2:-127.0.0.1}"
[[ -x "$BIN" ]] || { echo "Build peer first: cmake -B build && cmake --build build --target quic-p2p-peer"; exit 1; }

# For xquic+MOQ build, peer needs TLS cert/key. Generate if missing.
if [[ -f "$BIN" ]]; then
  if [[ ! -f "$CERT_DIR/server.crt" ]] || [[ ! -f "$CERT_DIR/server.key" ]]; then
    echo "[peer] Generating TLS cert in $CERT_DIR..."
    "$SCRIPT_DIR/gen_ssl_cert.sh" "$CERT_DIR"
  fi
  export P2P_SSL_CERT="${CERT_DIR}/server.crt"
  export P2P_SSL_KEY="${CERT_DIR}/server.key"
fi

echo "[peer] Signaling=$SIGNALING Turn=$COTURN_HOST"
exec "$BIN" --signaling "$SIGNALING" --turn-host "$COTURN_HOST" --turn-user juice_demo --turn-pass juice_password
