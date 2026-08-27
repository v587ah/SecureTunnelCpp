# SecureTunnelCpp

一个面向学习和后续工程化的 C++20 安全隧道骨架。当前版本只完成模块边界、配置校验和连接状态机，**不会接管真实流量，也没有自制加密算法**。

## 设计目标

- 低延迟：数据面优先 QUIC/UDP，避免 TCP-over-TCP。
- 高吞吐：后续通过 Wintun 环形缓冲、批量收发和少拷贝优化。
- 安全：使用成熟库实现 TLS 1.3、Noise 或 WireGuard，不自行实现密码原语。
- 可测试：虚拟网卡、传输和引擎通过接口隔离。
- 权限最小化：未来让高权限隧道服务与普通权限 UI 分进程运行。

## 当前结构

```text
SecureTunnelCpp/
├─ apps/
│  ├─ client_main.cpp       客户端演示入口（可选 --wintun）
│  ├─ server_main.cpp       服务端演示入口
│  └─ wintun_probe.cpp      Wintun 探测工具（需管理员）
├─ include/tunnel/
│  ├─ config.hpp
│  ├─ session.hpp
│  ├─ tunnel_engine.hpp
│  ├─ development_adapters.hpp
│  ├─ wintun_loader.hpp
│  ├─ wintun_virtual_interface.hpp
│  └─ windows_net_config.hpp  接口 IPv4/DNS（无全局路由）
├─ src/
├─ tests/
│  ├─ session_tests.cpp
│  └─ wintun_loader_tests.cpp
├─ third_party/wintun/      官方 Wintun 0.14.1
├─ docs/ARCHITECTURE.md
└─ CMakeLists.txt
```

## 模块作用

### `VirtualInterface`

屏蔽操作系统差异：

- Windows：`WintunVirtualInterface`（已实现生命周期与读写）
- 开发：`DevelopmentVirtualInterface`（回环，无需管理员）
- Linux：后续 `/dev/net/tun`

### `WintunLoader`

只负责加载 DLL 与解析导出符号。

### `WindowsNetConfigurator`

给 Wintun 适配器配置 IPv4 与接口 DNS。  
全局默认路由通过 `--global-route` 显式开启（需 QUIC 握手完成后才应用）。

### `SecureTransport`

屏蔽底层传输和加密实现。生产版本建议：

- 外层：MsQuic（QUIC + TLS 1.3）
- 内层：WireGuard 或经过审计的 Noise 实现
- 密码原语：BoringSSL、OpenSSL 3 或 libsodium

开发适配器没有密码学能力，不能用于生产。

### `TunnelEngine`

协调配置、网卡、传输和会话状态，不包含平台细节。只有安全握手完成后，会话才进入 `established`。

## 使用教程

完整使用说明、场景分步指南、Flutter / C# 集成示例见 **[docs/USAGE_TUTORIAL.md](docs/USAGE_TUTORIAL.md)**。  
Wintun 验证与远程 Linux/Windows 服务端对接见 **[docs/REMOTE_CONNECT.md](docs/REMOTE_CONNECT.md)**。

## 构建

需要 CMake 3.21+ 和支持 C++20 的编译器：

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

本机已使用 Visual Studio 2026 的 MSVC、CMake 和 Ninja 完成编译。

### 验证 Wintun（Windows）

```powershell
# 普通权限：只验证 DLL 导出符号
.\build\wintun_loader_tests.exe

# 管理员权限：创建适配器并启动会话（不改路由）
.\build\wintun_probe.exe

# 客户端可选接入真实网卡
.\build\tunnel_client.exe --wintun
```

## 安全原则

1. 禁止自行实现 AES、ChaCha20、X25519、随机数生成器或 TLS。
2. 禁止重复 nonce；密钥和方向必须隔离。
3. 默认禁用 0-RTT，直到服务端实现可靠的防重放策略。
4. 不伪造第三方网站身份、证书或 SNI。
5. 不在日志中保存密钥、完整数据包和用户内容。
6. 当前 Wintun 实现 **故意不改默认路由**，避免半成品劫持整机流量。

## Linux 服务端部署

参见 **[docs/LINUX_SERVER_DEPLOY.md](docs/LINUX_SERVER_DEPLOY.md)**：nftables NAT、TUN 配置、systemd 与证书说明。

```bash
sudo deploy/linux/scripts/install.sh "$(pwd)"
sudo /usr/local/lib/sectunnel/apply-nat.sh
sudo systemctl enable --now sectunnel-server
```

## 下一阶段

1. Linux 上完整集成 MsQuic（OpenSSL）与生产证书。
2. 接入成熟的 Noise/WireGuard 内层，不编写自定义密码算法。
3. Windows Service + Named Pipe，供 Flutter / C# UI 集成。
4. 使用 `ping`、`iperf3`、丢包网络仿真做性能基线。
