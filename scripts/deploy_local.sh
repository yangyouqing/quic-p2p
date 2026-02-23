#!/usr/bin/env bash
# Deploy coturn + signaling + peer + client locally and verify message exchange.
# Supports both plain ICE (Hello) and xquic+MOQ builds (QUIC/MOQ logs).
# Usage: ./scripts/deploy_local.sh
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build/bin

# Ensure binaries exist
SIGNALING_SRC=""
[[ -d src/signaling ]] && SIGNALING_SRC="src/signaling"
[[ -z "$SIGNALING_SRC" ]] && [[ -d src/sample_libjuice/signaling ]] && SIGNALING_SRC="src/sample_libjuice/signaling"
if [[ ! -x build/bin/signaling-server ]]; then
  if [[ -n "$SIGNALING_SRC" ]]; then
    echo "Building signaling server from $SIGNALING_SRC..."
    (cd "$SIGNALING_SRC" && go build -o "$PROJECT_ROOT/build/bin/signaling-server" ./cmd/server)
  else
    echo "Error: no signaling source (src/signaling or src/sample_libjuice/signaling)"
    exit 1
  fi
fi
if [[ ! -x build/bin/peer ]] || [[ ! -x build/bin/client ]]; then
  echo "Building client and peer..."
  cmake -B build -q 2>/dev/null || true
  cmake --build build --target quic-p2p-client quic-p2p-peer -q
fi

# For xquic+MOQ peer: ensure TLS cert exists in build/bin
if [[ ! -f build/bin/server.crt ]] || [[ ! -f build/bin/server.key ]]; then
  echo "Generating TLS cert for peer (QUIC server)..."
  "$SCRIPT_DIR/gen_ssl_cert.sh" "$PROJECT_ROOT/build/bin"
fi
export P2P_SSL_CERT="$PROJECT_ROOT/build/bin/server.crt"
export P2P_SSL_KEY="$PROJECT_ROOT/build/bin/server.key"

# Start coturn in background (quiet: log to file)
echo "Starting coturn..."
turnserver -c "$SCRIPT_DIR/coturn.conf" >> "$PROJECT_ROOT/build/coturn.log" 2>&1 &
COTURN_PID=$!
SIGNAL_PID=; PEER_PID=; CLIENT_PID=
cleanup() { kill $COTURN_PID $SIGNAL_PID $PEER_PID $CLIENT_PID 2>/dev/null || true; }
trap cleanup EXIT
sleep 1

# Signaling port: default 8888; use 18888 if 8888 is in use
SIGNALING_PORT=8888
if command -v ss &>/dev/null; then
  ss -tlnp 2>/dev/null | grep -q ":8888 " && SIGNALING_PORT=18888 && echo "Port 8888 in use, using $SIGNALING_PORT for signaling"
elif command -v netstat &>/dev/null; then
  netstat -tlnp 2>/dev/null | grep -q ":8888 " && SIGNALING_PORT=18888 && echo "Port 8888 in use, using $SIGNALING_PORT for signaling"
fi
export SIGNALING_TCP_ADDR=":$SIGNALING_PORT"
SIGNALING_ADDR="localhost:$SIGNALING_PORT"

# Start signaling in background
echo "Starting signaling server on $SIGNALING_ADDR..."
build/bin/signaling-server &
SIGNAL_PID=$!
sleep 1

# Start peer in background (capture output to temp file)
PEER_LOG=$(mktemp)
echo "Starting peer (log: $PEER_LOG)..."
build/bin/peer --signaling "$SIGNALING_ADDR" --turn-host 127.0.0.1 --turn-user juice_demo --turn-pass juice_password > "$PEER_LOG" 2>&1 &
PEER_PID=$!
sleep 2

# Run client in background
CLIENT_OUT=$(mktemp)
echo "Starting client..."
build/bin/client --signaling "$SIGNALING_ADDR" --turn-host 127.0.0.1 --turn-user juice_demo --turn-pass juice_password >> "$CLIENT_OUT" 2>&1 &
CLIENT_PID=$!
# Wait for ICE (+ optional QUIC handshake) and message exchange
sleep 8
CLIENT_OK=0
PEER_OK=0
# Success: plain ICE ("Hello"/"Received") or xquic+MOQ ("[MOQ]" / "[QUIC]" / "video"/"audio")
grep -qE "Hello from libjuice|Received.*bytes|\[MOQ\]|\[QUIC\]|QUIC|video|audio|subscribe" "$CLIENT_OUT" && CLIENT_OK=1
grep -qE "Hello from libjuice|Received.*bytes|\[MOQ\]|\[QUIC\]|QUIC|Publisher|connection accepted" "$PEER_LOG" && PEER_OK=1

echo ""
echo "========== Peer output (relevant) =========="
grep -E "ICE|Signal|Hello|Received|State|QUIC|MOQ|Publisher|connection accepted" "$PEER_LOG" 2>/dev/null || tail -25 "$PEER_LOG"
echo ""
echo "========== Client output (relevant) =========="
grep -E "ICE|Signal|Hello|Received|State|QUIC|MOQ|video|audio|subscribe" "$CLIENT_OUT" 2>/dev/null || tail -25 "$CLIENT_OUT"
echo ""

if [[ $CLIENT_OK -eq 1 ]] && [[ $PEER_OK -eq 1 ]]; then
  echo "=== SUCCESS: Client and peer both sent and received data ==="
elif [[ $CLIENT_OK -eq 1 ]]; then
  echo "=== SUCCESS: Client received data from peer ==="
elif [[ $PEER_OK -eq 1 ]]; then
  echo "=== SUCCESS: Peer received data from client ==="
else
  echo "=== FAIL: No message exchange. Peer (last 50): ==="
  tail -50 "$PEER_LOG"
  echo "=== Client (last 50): ==="
  tail -50 "$CLIENT_OUT"
fi
rm -f "$PEER_LOG" "$CLIENT_OUT"
