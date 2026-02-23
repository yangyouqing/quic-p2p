#!/usr/bin/env bash
# Build client and peer with xquic+MOQ. Installs BabaSSL to build/deps/babassl if needed.
# Usage: ./scripts/build_xquic.sh
# Then run: ./scripts/deploy_local.sh  (or run_peer/run_client manually)
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

SSL_PATH="${SSL_PATH:-}"
if [[ -z "$SSL_PATH" ]]; then
  if [[ -f build/deps/babassl/include/openssl/ssl.h ]]; then
    SSL_PATH="${PROJECT_ROOT}/build/deps/babassl"
  elif [[ -f /usr/local/babassl/include/openssl/ssl.h ]]; then
    SSL_PATH="/usr/local/babassl"
  fi
fi

if [[ -z "$SSL_PATH" ]] || [[ ! -f "$SSL_PATH/include/openssl/ssl.h" ]]; then
  echo "BabaSSL not found. Running install_babassl.sh..."
  "$SCRIPT_DIR/install_babassl.sh"
  SSL_PATH="${PROJECT_ROOT}/build/deps/babassl"
fi

echo "Building with SSL_PATH=$SSL_PATH"
cmake -B build -DSSL_PATH="$SSL_PATH" -DSSL_TYPE=babassl -DXQC_ENABLE_MOQ=ON
cmake --build build --target quic-p2p-client quic-p2p-peer
echo "Done. Run: ./scripts/deploy_local.sh"
