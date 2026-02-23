# P2P Signaling (libjuice + coturn)

Client and peer use the signaling server to exchange SDP/ICE candidates, then establish a P2P (or TURN-relayed) connection via libjuice and coturn.

## Layout

- **signaling/** – Production-grade Go signaling server (multi-room, config, health, graceful shutdown).
- **client/** – C client (typically gets role `peer1`, sends offer). Run **after** peer.
- **peer/** – C peer (typically gets role `peer2`, sends answer). Run **first**.
- **common/** – Shared C code (signal protocol, ICE/run loop).
- **coturn.conf.example** – Example coturn config for STUN/TURN.

## Build

From project root:

```bash
cmake -B build -DNO_TESTS=ON
cmake --build build --target quic-p2p-client quic-p2p-peer
```

Binaries: `build/bin/client`, `build/bin/peer`.

Go signaling server:

```bash
cd src/signaling && go build -o ../../build/bin/signaling-server ./cmd/server
```

## Run

1. Start coturn (copy `src/coturn.conf.example` to e.g. `/etc/turnserver.conf`, set `external-ip`, then):
   ```bash
   turnserver -c /etc/turnserver.conf
   ```

2. Start signaling server:
   ```bash
   ./build/bin/signaling-server
   ```
   Or with config: `./build/bin/signaling-server -config src/signaling/config.yaml`

3. Terminal 1 – peer (run first):
   ```bash
   ./build/bin/peer --signaling localhost:8888 --turn-host 127.0.0.1 --turn-user juice_demo --turn-pass juice_password
   ```

4. Terminal 2 – client:
   ```bash
   ./build/bin/client --signaling localhost:8888 --turn-host 127.0.0.1 --turn-user juice_demo --turn-pass juice_password
   ```

When connected, both sides should print "Hello from libjuice peer!".

## Options (client / peer)

| Option | Description |
|--------|-------------|
| `--signaling <host:port>` | Signaling server (default: localhost:8888) |
| `--turn-host <host>` | Coturn host (required) |
| `--turn-port <port>` | Coturn port (default: 3478) |
| `--turn-user`, `--turn-pass` | TURN credentials |
| `--room <id>` | Room ID (default: default) |
