#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="${PROJECT_ROOT}/build/bin/signaling-server"
if [[ ! -x "$BIN" ]]; then
  if [[ -d "$PROJECT_ROOT/src/signaling" ]]; then
    (cd "$PROJECT_ROOT/src/signaling" && go build -o "$PROJECT_ROOT/build/bin/signaling-server" ./cmd/server)
  elif [[ -d "$PROJECT_ROOT/src/sample_libjuice/signaling" ]]; then
    (cd "$PROJECT_ROOT/src/sample_libjuice/signaling" && go build -o "$PROJECT_ROOT/build/bin/signaling-server" ./cmd/server)
  else
    echo "Error: no signaling source found"
    exit 1
  fi
fi
echo "[signaling] TCP :8888 HTTP :8080"
exec "$BIN"
