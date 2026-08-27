# SecureTunnelCpp

**C++20 安全隧道 / VPN 框架** — QUIC/TLS 1.3 传输、Wintun 虚拟网卡、智能分流与全局路由。

> 使用 MsQuic、Wintun 等成熟组件，**不自行实现密码原语**。适合学习、二次开发与 Windows 客户端集成（C# / Flutter）。

---

## 功能概览

| 功能 | Windows 客户端 | Windows 服务端 | Linux 服务端 |
|------|:--------------:|:--------------:|:------------:|
| QUIC/TLS 1.3（MsQuic） | ✅ | ✅ | ⚠️ 需自行集成 MsQuic |
| Wintun 虚拟网卡 | ✅ | — | — |
| Linux TUN（`/dev/net/tun`） | — | — | ✅ |
| 数据面 Relay | ✅ | ✅ | ✅ |
| **智能分流**（`--smart-route`） | ✅ | — | — |
| **全局路由**（`--global-route`） | ✅ | — | — |
| nftables NAT 脚本 | — | — | ✅ |
| systemd 部署 | — | — | ✅ |

### VPN 两种模式（Windows 客户端）

| 模式 | 参数 | 行为 |
|------|------|------|
| **智能模式** | `--smart-route` | 国内直连；名单内国外/被墙域名与 CIDR 走隧道 |
| **全局模式** | `--global-route` | 全部流量经隧道（`0.0.0.0/0 → 10.66.66.1`） |

默认隧道网段：`10.66.66.0/24`（客户端 `10.66.66.2`，网关 `10.66.66.1`）。

---

## 技术栈

| 层次 | 技术 | 说明 |
|------|------|------|
| 语言 | **C++20** | CMake 3.21+，MSVC / GCC / Clang |
| 外层传输 | **[MsQuic](https://github.com/microsoft/msquic)** | QUIC + TLS 1.3（Windows 通过 NuGet 预编译包） |
| TLS（Windows） | **Schannel** | 证书存储 / PEM，由 MsQuic 调用 |
| 虚拟网卡（Windows） | **[Wintun](https://www.wintun.net/)** | WireGuard 官方用户态 TUN |
| 虚拟网卡（Linux） | **`/dev/net/tun`** | `LinuxTunVirtualInterface` |
| 内层封装 | **AES-GCM（BCrypt）** 或开发模式 | QUIC 之上的 AEAD 帧；空 PSK 时为开发透传 |
| 路由（Windows） | **IP Helper API** | 接口 IP/DNS、旁路路由、前缀路由、默认路由 |
| NAT（Linux） | **nftables** | MASQUERADE + 转发规则 |
| 进程管理 | **systemd** | `sectunnel-server.service` |
| 集成示例 | **C# / Flutter** | 进程方式启动 `tunnel_client.exe` |

### 架构

```text
┌─────────────────────────────────────────────────────────┐
│  Windows Client                                         │
│  tunnel_client.exe                                      │
│    ├─ WintunVirtualInterface (10.66.66.2)               │
│    ├─ SmartRouteManager / Global Route                  │
│    ├─ InnerSession (AEAD)                               │
│    └─ QuicSecureTransport ──UDP 44333──┐                │
└────────────────────────────────────────│────────────────┘
                                         ▼
┌─────────────────────────────────────────────────────────┐
│  Server (Windows / Linux)                               │
│  tunnel_server.exe                                      │
│    ├─ QuicSecureTransport (Listener)                    │
│    ├─ InnerSession (AEAD)                               │
│    ├─ VirtualInterface (dev / Linux TUN)                │
│    └─ TunnelEngine relay                                │
│         └─ Linux: sectun0 + nftables NAT → WAN           │
└─────────────────────────────────────────────────────────┘
```

---

## 快速开始（Windows 本机）

### 1. 构建

```powershell
# 需要 Visual Studio 2022+（含 C++ 桌面开发）、CMake、Ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

构建产物：`build/tunnel_client.exe`、`build/tunnel_server.exe`（自动复制 `msquic.dll`、`wintun.dll`）。

开发证书（首次）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/gen_dev_cert.ps1
```

### 2. 启动（两个窗口）

**Server：**

```text
scripts\run_server.bat
```

**Client（管理员 — 智能 VPN）：**

```text
scripts\run_client_wintun_smart.bat
```

或全局 VPN：`scripts\run_client_wintun_global.bat`

### 3. 验证 Wintun

```text
scripts\verify_wintun.vbs     ← 双击，自动检测网卡
```

---

## 命令行参考

### 客户端 `tunnel_client.exe`

| 参数 | 说明 |
|------|------|
| `--quic` | 启用 QUIC/TLS 1.3（生产必选） |
| `--wintun` | Wintun 虚拟网卡（**需管理员**） |
| `--smart-route` | 智能分流（与 `--global-route` 互斥） |
| `--global-route` | 全局默认路由（**需管理员**） |
| `--relay` | 运行数据面 relay |
| `--relay-seconds:N` | relay 持续 N 秒 |
| `--insecure` | 跳过证书校验（**仅 localhost**） |
| `--endpoint:<host>` | 服务器地址，默认 `127.0.0.1` |
| `--port:<port>` | UDP 端口，默认 `44333` |
| `--smart-domains:<file>` | 智能分流域名列表 |
| `--smart-cidrs:<file>` | 智能分流 CIDR 列表 |

**示例：**

```powershell
# 本机开发
.\build\tunnel_client.exe --quic --insecure --wintun --smart-route

# 远程生产（需导入服务端证书，不要用 --insecure）
.\build\tunnel_client.exe --quic --wintun --smart-route --endpoint:203.0.113.10 --port:44333
```

### 服务端 `tunnel_server.exe`

| 参数 | 说明 |
|------|------|
| `--quic` | QUIC/TLS 监听（UDP 44333） |
| `--relay` | 持续数据面 relay |
| `--tun` | Linux TUN 网卡（Linux 专用） |
| `--cert_hash:<hex>` | Windows 证书存储 SHA1 指纹 |
| `--cert_file:<path>` | Linux PEM 证书 |
| `--key_file:<path>` | Linux PEM 私钥 |

---

## 项目结构

```text
SecureTunnelCpp/
├─ apps/                    # client_main, server_main, wintun_probe
├─ include/tunnel/          # 公共头文件
├─ src/                     # 核心实现
│  ├─ quic_secure_transport.cpp
│  ├─ wintun_virtual_interface.cpp
│  ├─ smart_route_manager.cpp
│  ├─ windows_net_config.cpp
│  └─ linux_tun_virtual_interface.cpp
├─ config/                  # 智能分流默认名单
│  ├─ smart_route_domains.txt
│  └─ smart_route_cidrs.txt
├─ scripts/                 # Windows 启动与验证脚本
├─ deploy/linux/            # Linux NAT / TUN / systemd
├─ docs/                    # 详细教程
├─ examples/                # C# / Flutter 集成示例
├─ tests/                   # 单元测试
└─ third_party/
   ├─ wintun/               # Wintun DLL + 头文件
   └─ msquic/nuget/         # MsQuic Windows 预编译包
```

---

## 文档

| 文档 | 内容 |
|------|------|
| [docs/USAGE_TUTORIAL.md](docs/USAGE_TUTORIAL.md) | 完整使用教程、场景分步、C#/Flutter 集成 |
| [docs/REMOTE_CONNECT.md](docs/REMOTE_CONNECT.md) | 远程对接、证书导入、Windows/Linux 服务端选择 |
| [docs/LINUX_SERVER_DEPLOY.md](docs/LINUX_SERVER_DEPLOY.md) | Linux VPS：编译、NAT、systemd |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 模块设计与状态机 |

---

## 测试

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

主要测试：`session_tests`、`inner_session_tests`、`data_plane_relay_tests`、`quic_handshake_tests`（Windows+MsQuic）、`smart_route_list_tests`、`wintun_loader_tests`。

---

## 平台与限制

### 已验证

- Windows 10/11 + MSVC：QUIC 握手、Wintun 创建、智能/全局 VPN 本机联调
- Server 断线后持续监听（relay 循环 + 会话重置）

### 已知限制

1. **Linux QUIC**：CMake 仅在 Windows 自动链接 MsQuic；Linux 上 `--quic` 需自行集成 [MsQuic Linux 构建](https://github.com/microsoft/msquic) 后才能与 Windows 客户端互通。
2. **Server 端口**：当前固定 `44333`，`sectunnel.env` 中的 `QUIC_PORT` 仅影响防火墙规则。
3. **内层 PSK 默认空**：开发模式无 AEAD；生产应配置 `InnerSessionConfig::psk_hex`。
4. **Windows Service / Named Pipe**：文档与示例草案已写，代码尚未实现。
5. **远程 `--insecure`**：配置校验拒绝，必须使用受信任 TLS 证书。

---

## 安全原则

1. 不自行实现 AES、ChaCha20、X25519、TLS 等密码原语。
2. 默认禁用 0-RTT，防止重放。
3. `--insecure` 仅允许 `127.0.0.1` / `localhost`。
4. 路由变更（全局/智能）仅在 QUIC 握手成功后应用；退出时自动清理。
5. 不在日志中记录密钥或完整数据包内容。

---

## 路线图

- [ ] Linux MsQuic 集成（CMake + OpenSSL）
- [ ] Server 可配置 `--port:`
- [ ] 客户端 TLS 证书 pinning（`--cert_hash`）
- [ ] Windows Service + Named Pipe（UI 集成）
- [ ] 内层 WireGuard / Noise 生产实现
- [ ] 性能基线（iperf3、丢包仿真）

---

## 第三方许可

- [Wintun](https://www.wintun.net/) — WireGuard LLC
- [MsQuic](https://github.com/microsoft/msquic) — MIT（Microsoft）

---

## 免责声明

本项目仅供 **学习、研究与合法授权网络测试**。使用者须遵守当地法律法规，作者不对滥用行为负责。
