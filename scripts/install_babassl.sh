#!/usr/bin/env bash
# Build and install BabaSSL/Tongsuo to build/deps/babassl for xquic+MOQ (static libs).
# Usage: ./scripts/install_babassl.sh
# Then: cmake -B build -DSSL_PATH=$PWD/build/deps/babassl && cmake --build build
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"
PREFIX="${PROJECT_ROOT}/build/deps/babassl"
SRC_DIR="${PROJECT_ROOT}/build/deps/babassl_src"

if [[ -f "$PREFIX/include/openssl/ssl.h" ]] && [[ -f "$PREFIX/lib/libssl.a" ]]; then
  echo "BabaSSL already installed at $PREFIX"
  echo "SSL_PATH=$PREFIX"
  exit 0
fi

echo "Installing BabaSSL/Tongsuo to $PREFIX (static libs for xquic)..."
mkdir -p build/deps
if [[ ! -d "$SRC_DIR/.git" ]]; then
  echo "Cloning Tongsuo (BabaSSL)..."
  git clone --depth 1 https://github.com/Tongsuo-Project/Tongsuo.git "$SRC_DIR"
fi
cd "$SRC_DIR"
./config --prefix="$PREFIX" no-shared no-tests
make -j"$(nproc 2>/dev/null || echo 2)"
make install_sw
cd "$PROJECT_ROOT"
echo "BabaSSL installed at $PREFIX"
echo "Build with: cmake -B build -DSSL_PATH=$PREFIX && cmake --build build --target quic-p2p-client quic-p2p-peer"
echo "SSL_PATH=$PREFIX"
