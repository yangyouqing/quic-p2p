# P2P 部署脚本

本目录包含 coturn、信令、peer、client 的部署与一键本地验证脚本。

## 脚本说明

| 脚本 | 说明 |
|------|------|
| `run_coturn.sh` | 启动 coturn（STUN/TURN），使用 `scripts/coturn.conf`，监听 127.0.0.1:3478 |
| `run_signaling.sh` | 启动 Go 信令服务，TCP :8888，HTTP/WebSocket :8080 |
| `run_peer.sh [signaling] [turn-host]` | 启动 peer，默认 `localhost:8888`、`127.0.0.1` |
| `run_client.sh [signaling] [turn-host]` | 启动 client，默认同上 |
| `deploy_local.sh` | 本地一键部署：依次启动 coturn、信令、peer、client，并校验两端能否收发消息 |

## 依赖

- **coturn**：系统安装 `turnserver`（如 `apt install coturn`）
- **Go**：用于信令（若未构建则脚本内会 `go build`）
- **CMake + C 编译器**：用于 client/peer（若未构建则需先执行根目录 `cmake -B build -DNO_TESTS=ON && cmake --build build --target quic-p2p-client quic-p2p-peer`）

## 手动部署（四步）

```bash
# 终端 1：coturn
./scripts/run_coturn.sh

# 终端 2：信令
./scripts/run_signaling.sh

# 终端 3：先启动 peer
./scripts/run_peer.sh

# 终端 4：再启动 client
./scripts/run_client.sh
```

当 ICE 连接成功后，两端会打印 "Hello from libjuice peer!" 或 "Received N bytes: ..."。

## 一键本地验证

```bash
./scripts/deploy_local.sh
```

会自动启动 coturn、信令、peer、client，等待约 6 秒后检查 client/peer 日志是否包含收到对端数据，并输出 `=== SUCCESS: Client received data from peer ===` 或 peer 侧成功信息。coturn 日志写入 `build/coturn.log`。

## 参数示例

指定信令和 TURN 主机（如远程或不同端口）：

```bash
./scripts/run_peer.sh   my-server:8888  192.168.1.100
./scripts/run_client.sh my-server:8888  192.168.1.100
```
