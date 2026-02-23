#!/usr/bin/env bash
# Generate a self-signed certificate and key for the QUIC peer (MOQ Publisher).
# Usage: ./scripts/gen_ssl_cert.sh [output_dir]
# Output: server.crt and server.key in output_dir (default: current directory).
# Set P2P_SSL_CERT and P2P_SSL_KEY to these paths when running the peer, or run peer from output_dir.

set -e
OUT="${1:-.}"
mkdir -p "$OUT"
CERT="$OUT/server.crt"
KEY="$OUT/server.key"

if command -v openssl &>/dev/null; then
  openssl req -x509 -newkey rsa:2048 -nodes -keyout "$KEY" -out "$CERT" \
    -days 365 -subj "/CN=p2p-moq/O=quic-p2p"
  echo "Generated $CERT and $KEY (openssl)"
elif command -v babassl &>/dev/null; then
  babassl req -x509 -newkey rsa:2048 -nodes -keyout "$KEY" -out "$CERT" \
    -days 365 -subj "/CN=p2p-moq/O=quic-p2p"
  echo "Generated $CERT and $KEY (babassl)"
else
  echo "Error: need openssl or babassl in PATH" >&2
  exit 1
fi
chmod 600 "$KEY"
echo "Peer usage: P2P_SSL_CERT=$CERT P2P_SSL_KEY=$KEY ./bin/peer ..."
