# SecureTunnel 使用教程

本文档面向 **最终使用者** 与 **二次开发者**，说明如何编译、部署、联调 SecureTunnelCpp，以及如何在 **Flutter** 与 **C#** 应用中集成隧道客户端。

> 相关文档：
> - [ARCHITECTURE.md](ARCHITECTURE.md) — 架构与模块边界
> - [LINUX_SERVER_DEPLOY.md](LINUX_SERVER_DEPLOY.md) — Linux 服务端 NAT / systemd 部署

---

## 目录

1. [项目概览](#1-项目概览)
2. [环境准备](#2-环境准备)
3. [编译与测试](#3-编译与测试)
4. [证书准备](#4-证书准备)
5. [使用场景分步教程](#5-使用场景分步教程)
6. [命令行参数完整参考](#6-命令行参数完整参考)
7. [网络拓扑与地址规划](#7-网络拓扑与地址规划)
8. [故障排查](#8-故障排查)
9. [与 Flutter / C# 集成总览](#9-与-flutter--c-集成总览)
10. [C# 调用示例](#10-c-调用示例)
11. [Flutter 调用示例](#11-flutter-调用示例)
12. [推荐的生产架构](#12-推荐的生产架构)
13. [安全注意事项](#13-安全注意事项)
14. [示例代码文件清单](#14-示例代码文件清单)

---

## 1. 项目概览

SecureTunnelCpp 是一个 **C++20 安全隧道** 项目，当前已实现：

| 能力 | 状态 | 说明 |
|------|------|------|
| QUIC + TLS 1.3 传输 | ✅ | Windows 使用 MsQuic + Schannel |
| 内层 AEAD 加密帧 | ✅ | 开发模式 / Windows BCrypt AES-256-GCM |
| Wintun 虚拟网卡 | ✅ | Windows，需管理员 |
| 全局默认路由 | ✅ | `--global-route`，握手成功后才生效 |
| Linux TUN + NAT 脚本 | ✅ | 见 Linux 部署文档 |
| Flutter / C# 官方 SDK | ⏳ | 当前通过 **进程调用** 或 **规划中的 Named Pipe** 集成 |

### 数据路径

```text
[Windows 客户端]
  应用程序流量
    → 系统路由表（0.0.0.0/0 → 10.66.66.1）
    → Wintun 虚拟网卡（10.66.66.2）
    → TunnelEngine 数据面 relay
    → 内层 AEAD 加密
    → QUIC/UDP + TLS 1.3
    → 互联网

[Linux 服务端]
  QUIC 监听 UDP 44333
    → 解封装
    → TUN sectun0（10.66.66.1/24）
    → nftables MASQUERADE
    → 公网
```

### 可执行文件

编译成功后，在 `build-agent/`（或 `build/`）目录下：

| 文件 | 角色 |
|------|------|
| `tunnel_client.exe` | Windows 客户端 |
| `tunnel_server.exe` | Windows / Linux 服务端 |
| `wintun_probe.exe` | Wintun 探测（管理员） |
| `*.dll` | 运行时依赖（`msquic.dll`、`wintun.dll` 等） |

---

## 2. 环境准备

### Windows 客户端开发机

| 组件 | 版本要求 |
|------|----------|
| 操作系统 | Windows 10 1809+ / Windows 11 |
| 编译器 | Visual Studio 2022+（含 C++ 桌面开发） |
| CMake | 3.21+ |
| Ninja | 推荐 |
| 权限 | Wintun / 改路由需要 **管理员** |

### Linux 服务端

| 组件 | 版本要求 |
|------|----------|
| 系统 | Ubuntu 22.04+ / Debian 12+ |
| 工具 | `cmake`、`ninja`、`nftables`、`iproute2` |
| 网络 | 公网 UDP 端口可达（默认 44333） |

### 运行时文件布局（Windows）

将以下文件放在 **同一目录**（例如 `build-agent/`）：

```text
build-agent/
├── tunnel_client.exe
├── tunnel_server.exe
├── msquic.dll
├── wintun.dll
└── certs/dev/thumbprint.txt   ← 开发证书指纹（可选）
```

---

## 3. 编译与测试

在 **Developer Command Prompt for VS** 或已配置 MSVC 的终端：

```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp

cmake -S . -B build-agent -G Ninja
cmake --build build-agent
ctest --test-dir build-agent --output-on-failure
```

期望结果：**全部测试通过**（通常 9/9）。

### 快速验证 Wintun

```powershell
# 1. 验证 DLL 符号（无需管理员）
.\build-agent\wintun_loader_tests.exe

# 2. 创建适配器（需管理员 PowerShell）
.\build-agent\wintun_probe.exe
```

---

## 4. 证书准备

### 4.1 本地开发（Windows）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\gen_dev_cert.ps1
```

生成内容：

- 证书存入 `Cert:\CurrentUser\My`
- 指纹写入 `certs\dev\thumbprint.txt`
- FriendlyName: `SecureTunnel-Dev`
- DNS: `localhost`, `127.0.0.1`

客户端本地联调时可加 `--insecure`（**仅限 127.0.0.1**）。

### 4.2 生产环境（Linux 服务端）

```bash
sudo mkdir -p /etc/sectunnel/certs
sudo openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout /etc/sectunnel/certs/server.key \
  -out /etc/sectunnel/certs/server.crt \
  -days 365 -subj "/CN=your.domain.com"
sudo chmod 600 /etc/sectunnel/certs/server.key
```

生产客户端 **禁止** 使用 `--insecure`，应校验正式 CA 或内网 PKI 证书。

---

## 5. 使用场景分步教程

### 场景 A：开发模式联调（最简单，无需管理员）

**目的**：验证工程结构、状态机、开发传输，不碰真实网卡。

```powershell
.\build-agent\tunnel_client.exe
.\build-agent\tunnel_server.exe
```

特点：

- 使用内存中的 `DevelopmentVirtualInterface`
- 无真实 QUIC、无 Wintun
- 适合跑单元测试、理解模块边界

---

### 场景 B：本机 QUIC 握手 + 数据面（推荐第一步）

**目的**：验证 MsQuic、TLS 1.3、数据面 relay。

**终端 1 — 服务端：**

```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp
.\build-agent\tunnel_server.exe --quic --relay
```

**终端 2 — 客户端：**

```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp
.\build-agent\tunnel_client.exe --quic --insecure --relay
```

成功标志：

```text
客户端状态：连接已建立，会话代数：...
数据面 relay 运行中（5 秒）...
```

---

### 场景 C：Wintun 虚拟网卡（不劫持整机流量）

**目的**：创建真实虚拟网卡，配置 IP/DNS，但不改默认路由。

```powershell
# 管理员 PowerShell
.\build-agent\tunnel_client.exe --quic --wintun --insecure
```

效果：

- 创建 `SecureTunnel` 适配器
- 客户端 IP：`10.66.66.2/24`
- DNS：`1.1.1.1`
- **不改** `0.0.0.0/0` 默认路由

验证：

```powershell
ipconfig
Get-NetIPAddress -InterfaceAlias "SecureTunnel"
```

---

### 场景 D：整机流量走隧道（全局路由）

**目的**：连接成功后，将默认路由指向隧道网关 `10.66.66.1`。

```powershell
# 管理员 PowerShell
# 终端 1
.\build-agent\tunnel_server.exe --quic --relay

# 终端 2
.\build-agent\tunnel_client.exe --quic --insecure --wintun --global-route
```

关键机制：

1. 先完成 QUIC/TLS 握手
2. 为服务器 IP 添加 `/32` **旁路路由**（防止 QUIC 环路）
3. 添加 `0.0.0.0/0 → 10.66.66.1` 默认路由
4. 持续运行数据面 relay
5. **Ctrl+C** 退出时自动清理路由

验证：

```powershell
route print 0.0.0.0
ping 1.1.1.1
```

> 注意：若服务端在本机，必须保持 `--insecure` 且 endpoint 为 `127.0.0.1`；远程服务器见场景 E。

---

### 场景 E：Windows 客户端 + Linux 远程服务端（生产形态）

#### 步骤 1：部署 Linux 服务端

完整步骤见 [LINUX_SERVER_DEPLOY.md](LINUX_SERVER_DEPLOY.md)，核心命令：

```bash
cmake -S . -B build -G Ninja && cmake --build build
sudo install -m 0755 build/tunnel_server /usr/local/bin/
sudo deploy/linux/scripts/install.sh "$(pwd)"
sudo /usr/local/lib/sectunnel/apply-nat.sh
sudo systemctl enable --now sectunnel-server
```

确认 UDP 44333 已放行：

```bash
sudo ss -ulnp | grep 44333
sudo nft list table ip sectunnel
```

#### 步骤 2：Windows 客户端连接

```powershell
# 管理员 PowerShell
.\build-agent\tunnel_client.exe `
  --quic `
  --wintun `
  --global-route `
  --endpoint:203.0.113.10 `
  --port:44333
```

参数说明：

- `203.0.113.10` 替换为你的 VPS 公网 IP
- 远程连接 **不要** 加 `--insecure`
- 需确保客户端信任服务端 TLS 证书（正式 CA 或手动导入）

---

## 6. 命令行参数完整参考

### 6.1 客户端 `tunnel_client.exe`

| 参数 | 必填 | 说明 |
|------|------|------|
| `--quic` | 生产必填 | 启用 QUIC/TLS 1.3（MsQuic） |
| `--wintun` | 真实流量必填 | 使用 Wintun 虚拟网卡（管理员） |
| `--global-route` | 否 | 整机流量走隧道；需同时 `--wintun --quic` |
| `--relay` | 否 | 运行数据面；`--global-route` 时自动启用 |
| `--insecure` | 否 | 跳过证书校验；**仅允许** `127.0.0.1` / `localhost` |
| `--endpoint:<host>` | 否 | 服务器地址，默认 `127.0.0.1` |
| `--port:<port>` | 否 | UDP 端口，默认 `44333` |
| `--cert_hash:<hex>` | 否 | Windows 证书存储 SHA1 指纹；默认读 `certs/dev/thumbprint.txt` |

#### 组合约束

| 组合 | 是否合法 |
|------|----------|
| `--global-route` 无 `--wintun` | ❌ |
| `--global-route` 无 `--quic` | ❌ |
| `--insecure` + 远程 IP | ❌（配置校验拒绝） |
| 仅 `--quic --relay` | ✅ 开发联调 |
| `--quic --wintun --global-route` | ✅ 整机隧道（管理员） |

### 6.2 服务端 `tunnel_server.exe`

| 参数 | 平台 | 说明 |
|------|------|------|
| `--quic` | 全平台 | QUIC/TLS 监听 |
| `--relay` | 全平台 | 持续数据面 |
| `--relay-seconds:<n>` | 全平台 | relay 运行 n 秒后退出 |
| `--tun` | Linux | 使用 `/dev/net/tun` |
| `--cert_hash:<hex>` | Windows | Schannel 证书指纹 |
| `--cert_file:<path>` | Linux | PEM 证书 |
| `--key_file:<path>` | Linux | PEM 私钥 |

环境变量（Linux）：

- `TLS_CERT_FILE`
- `TLS_KEY_FILE`

---

## 7. 网络拓扑与地址规划

### 默认隧道网段

| 角色 | 接口 | 地址 |
|------|------|------|
| Windows 客户端 | Wintun `SecureTunnel` | `10.66.66.2/24` |
| Linux 服务端 | TUN `sectun0` | `10.66.66.1/24` |
| 网段 | — | `10.66.66.0/24` |

### 路由行为（Windows 客户端）

| 模式 | 系统路由变化 |
|------|-------------|
| 仅 `--wintun` | 只配置接口 IP/DNS，不改默认路由 |
| `--global-route` | 添加服务器 `/32` 旁路 + `0.0.0.0/0 → 10.66.66.1` |

### 端口

| 协议 | 默认端口 |
|------|----------|
| QUIC/UDP | 44333 |

生产环境可改为 UDP 443，但需同步修改客户端 `--port` 与服务端 `QUIC_PORT`。

---

## 8. 故障排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| `客户端启动失败（QUIC...）` | 缺少 `msquic.dll` 或证书 | 确认 DLL 与 exe 同目录；检查 `thumbprint.txt` |
| `Wintun...管理员` | 权限不足 | 以管理员运行 PowerShell |
| QUIC 握手失败 | 服务端未启动 / UDP 被墙 | 检查服务端 `--quic --relay`；放行 UDP 44333 |
| `--insecure` 报错 | endpoint 不是 localhost | 远程服务器去掉 `--insecure`，配置正式证书 |
| 全局路由后断网 | 服务端 NAT 未配置 | 在 Linux 执行 `apply-nat.sh` |
| 能连上但无法上网 | 数据面未 relay | 加 `--relay` 或 `--global-route` |
| 退出后仍无法上网 | 路由未清理 | 手动 `route delete 0.0.0.0`；重启网络适配器 |
| Ctrl+C 后 QUIC 仍占用 | 进程未完全退出 | 任务管理器结束 `tunnel_client.exe` |
| VS 运行中文乱码 | 控制台代码页为 GBK，源码为 UTF-8 | 重新编译（已内置 UTF-8 修复）；或 VS 菜单 **工具 → 选项 → 环境 → 预览 → 使用 UTF-8** |

### VS 中文乱码专项说明

本项目字符串为 **UTF-8**。若在控制台看到 `鏈嶅姟绔` 这类乱码，按顺序尝试：

1. **必须重新编译**（修改了 `console_utf8.cpp`，旧 exe 无效）  
   - VS：**生成 → 重新生成 CMake 项目**  
   - 注意 VS 默认输出可能在 `out\build\x64-Debug\`，不要误跑旧的 `build-agent\` 里的 exe
2. **用外部终端运行**（已配置 `launch.vs.json` 的 `externalTerminal`）  
   或在 PowerShell 中手动运行 exe，不要用 VS 内置“输出”窗口
3. PowerShell 先执行：`chcp 65001`
4. VS 2022+：**工具 → 选项 → 环境 → 预览 → 使用 UTF-8 作为全局语言支持**，重启 VS

程序已在启动时通过 `WriteConsoleW` 输出中文（不依赖控制台代码页）。

正常输出示例：

```text
QUIC 服务端监听 UDP 44333，等待客户端连接...
服务端状态：连接已建立，传输：QUIC/TLS1.3，网卡：开发适配器
```

### 诊断命令

```powershell
# Windows 客户端
route print
ipconfig /all
Get-NetAdapter | Where-Object Status -eq 'Up'
Test-NetConnection -ComputerName 127.0.0.1 -Port 44333
```

```bash
# Linux 服务端
journalctl -u sectunnel-server -e
sudo nft list table ip sectunnel
sudo tcpdump -i sectun0 -n
sysctl net.ipv4.ip_forward
```

---

## 9. 与 Flutter / C# 集成总览

当前 C++ 核心 **没有** 直接暴露 C API / DLL 导出，UI 层有三种集成方式：

```text
┌─────────────────────────────────────────────────────────────┐
│  Flutter UI（普通权限）                                      │
│    │ MethodChannel                                           │
│    ▼                                                         │
│  C# / C++ Windows 宿主（可选，负责提权与 IPC）               │
│    │ Named Pipe（规划中）或 Process.Start                    │
│    ▼                                                         │
│  tunnel_client.exe（管理员 / Windows Service）               │
│    │ Wintun + 路由 + QUIC                                    │
│    ▼                                                         │
│  SecureTunnelCpp 核心（TunnelEngine）                        │
└─────────────────────────────────────────────────────────────┘
```

| 方式 | 适用场景 | 权限 | 当前可用 |
|------|----------|------|----------|
| **A. 进程调用** | 开发联调、无 Wintun | 普通用户 | ✅ |
| **B. 提权进程** | Wintun + 全局路由 | 管理员 / UAC | ✅ |
| **C. Windows Service + Named Pipe** | 生产 UI | UI 普通权限 | ⏳ 协议草案已给出 |

---

## 10. C# 调用示例

完整可运行示例位于：

```text
examples/csharp/SecureTunnel.Example/
├── SecureTunnel.Example.csproj
├── TunnelProcessRunner.cs      ← 推荐：封装 Process 启动
├── NamedPipeClient.cs          ← 规划：Service IPC 客户端
└── Program.cs                  ← 控制台 Demo
```

### 10.1 编译并运行 C# 示例

```powershell
cd examples\csharp\SecureTunnel.Example
dotnet run
```

### 10.2 核心代码：启动隧道进程

```csharp
await using var runner = new TunnelProcessRunner();

runner.OutputReceived += (_, line) => Console.WriteLine($"[隧道] {line}");

var options = new TunnelClientOptions
{
    Endpoint = "127.0.0.1",
    Port = 44333,
    UseQuic = true,
    Insecure = true,
    Relay = true,
};

var buildDir = @"C:\Users\Admin\Desktop\SecureTunnelCpp\build-agent";
await runner.StartAsync(options, buildDir, runAsAdmin: false);

await Task.Delay(TimeSpan.FromSeconds(30));
await runner.StopAsync();
```

### 10.3 生产模式：Wintun + 全局路由（需 UAC）

```csharp
var options = new TunnelClientOptions
{
    Endpoint = "203.0.113.10",
    Port = 44333,
    UseQuic = true,
    UseWintun = true,
    GlobalRoute = true,
    Relay = true,
    Insecure = false,
};

// runAsAdmin: true → 弹出 UAC，以管理员启动 tunnel_client.exe
await runner.StartAsync(options, buildDir, runAsAdmin: true);
```

### 10.4 WPF / WinUI 按钮点击示例

```csharp
private TunnelProcessRunner? _runner;

private async void ConnectButton_Click(object sender, RoutedEventArgs e)
{
    _runner = new TunnelProcessRunner();
    _runner.OutputReceived += (_, line) => Dispatcher.Invoke(() => LogTextBox.AppendText(line + "\n"));

    await _runner.StartAsync(
        new TunnelClientOptions
        {
            Endpoint = ServerTextBox.Text,
            Port = ushort.Parse(PortTextBox.Text),
            UseQuic = true,
            UseWintun = true,
            GlobalRoute = FullTunnelCheckBox.IsChecked == true,
            Relay = true,
        },
        AppContext.BaseDirectory,
        runAsAdmin: true);
}

private async void DisconnectButton_Click(object sender, RoutedEventArgs e)
{
    if (_runner != null)
    {
        await _runner.StopAsync();
        _runner = null;
    }
}
```

### 10.5 Named Pipe 协议草案（Windows Service）

当隧道核心迁入 Windows Service 后，UI 通过 Pipe 发送 JSON 命令：

**请求示例：**

```json
{
  "command": "connect",
  "endpoint": "203.0.113.10",
  "port": 44333,
  "useQuic": true,
  "useWintun": true,
  "globalRoute": true,
  "insecure": false
}
```

**响应示例：**

```json
{
  "state": "established",
  "connected": true,
  "endpoint": "203.0.113.10"
}
```

帧格式：`[4 字节 little-endian 长度][UTF-8 JSON 正文]`

C# 客户端实现见 `NamedPipeClient.cs`。

---

## 11. Flutter 调用示例

示例文件：

```text
examples/flutter/
├── lib/
│   ├── tunnel_service.dart     ← Process + PlatformChannel 封装
│   └── main_example.dart       ← 简单 UI Demo
└── windows/runner/
    └── tunnel_method_channel.cpp  ← Windows 宿主 Channel 示例
```

### 11.1 方式 A：Dart 直接启动进程（开发模式）

适合 `--quic --relay`，**不需要** Wintun / 管理员。

```dart
final service = TunnelProcessService(
  workingDirectory: r'C:\Users\Admin\Desktop\SecureTunnelCpp\build-agent',
);

await service.start(
  TunnelClientOptions(
    endpoint: '127.0.0.1',
    port: 44333,
    useQuic: true,
    insecure: true,
    relay: true,
  ),
);

service.output.listen((line) => debugPrint('[tunnel] $line'));

await Future.delayed(const Duration(seconds: 30));
await service.stop();
```

依赖：仅 `dart:io`，无需额外 package。

### 11.2 方式 B：Platform Channel（生产模式）

Wintun + 全局路由 **不应** 由 Flutter 进程直接提权，应交给 Windows 宿主或 Windows Service。

**Dart 侧：**

```dart
final platform = TunnelPlatformService();

await platform.connect(
  TunnelClientOptions(
    endpoint: '203.0.113.10',
    port: 44333,
    useQuic: true,
    useWintun: true,
    globalRoute: true,
    relay: true,
  ),
);

final status = await platform.getStatus();
debugPrint('state=${status['state']}');
```

**Windows Runner（C++）** 需注册 `sectunnel/control` Channel，示例见：

`examples/flutter/windows/runner/tunnel_method_channel.cpp`

在 `flutter_window.cpp` 中调用：

```cpp
RegisterTunnelMethodChannel(flutter_controller_->engine()->messenger());
```

**推荐**：Runner 的 `connect` 方法内部调用 C# `TunnelProcessRunner`（通过 COM / 原生插件）或直接连接 Named Pipe 到 Windows Service。

### 11.3 完整 UI 示例

运行 `main_example.dart` 中的 `TunnelDemoPage`，包含：

- Endpoint / Port 输入框
- 「开发模式连接（Process）」按钮
- 「生产模式连接（Platform Channel）」按钮
- 日志输出区域

将 `workingDirectory` 改为你的 `build-agent` 绝对路径。

### 11.4 Flutter + C# 混合方案（推荐 Windows 桌面）

```text
Flutter UI (Dart)
  └─ MethodChannel "sectunnel/control"
       └─ C# 类库 SecureTunnel.Example.TunnelProcessRunner
            └─ tunnel_client.exe（管理员）
```

步骤：

1. 创建 C# 类库，引用 `TunnelProcessRunner.cs`
2. 用 `flutter create` 创建 Windows 项目
3. 在 Runner 中通过 C++/CLI 或进程间调用 C# 类库
4. Flutter 只负责 UI，不直接处理管理员权限

---

## 12. 推荐的生产架构

```text
┌──────────────────┐
│  Flutter / WPF   │  普通用户权限
│  UI 层           │
└────────┬─────────┘
         │ Named Pipe / gRPC over localhost
         ▼
┌──────────────────┐
│  TunnelService   │  Windows Service（LocalSystem 或受限账户 + CAP）
│  tunnel_core     │  长时间运行，持有 Wintun 会话
└────────┬─────────┘
         │ QUIC/UDP
         ▼
┌──────────────────┐
│  Linux VPS       │  systemd + nftables NAT
│  tunnel_server   │
└──────────────────┘
```

实施优先级：

1. ✅ 现阶段：C# / Flutter 通过 `Process.Start` 调 `tunnel_client.exe`
2. ⏳ 下一步：实现 Windows Service + Named Pipe 服务端
3. ⏳ 后续：可选导出 C ABI 供 FFI 直接调用 `TunnelEngine`

---

## 13. 安全注意事项

1. **禁止** 在生产环境使用 `--insecure`
2. **禁止** 将 PSK / 私钥写入 git 或 UI 配置文件明文
3. Wintun + 全局路由会接管整机流量，务必确认服务端 NAT 正常后再开启
4. 退出客户端后确认路由已恢复（Ctrl+C 正常退出会自动清理）
5. Linux 防火墙默认 drop，仅放行必要 UDP/SSH
6. 日志不要打印密钥、完整 IP 包内容

---

## 14. 示例代码文件清单

| 路径 | 说明 |
|------|------|
| [examples/csharp/SecureTunnel.Example/TunnelProcessRunner.cs](../examples/csharp/SecureTunnel.Example/TunnelProcessRunner.cs) | C# 进程封装 |
| [examples/csharp/SecureTunnel.Example/NamedPipeClient.cs](../examples/csharp/SecureTunnel.Example/NamedPipeClient.cs) | Named Pipe 客户端草案 |
| [examples/csharp/SecureTunnel.Example/Program.cs](../examples/csharp/SecureTunnel.Example/Program.cs) | C# 控制台 Demo |
| [examples/flutter/lib/tunnel_service.dart](../examples/flutter/lib/tunnel_service.dart) | Flutter 服务封装 |
| [examples/flutter/lib/main_example.dart](../examples/flutter/lib/main_example.dart) | Flutter UI Demo |
| [examples/flutter/windows/runner/tunnel_method_channel.cpp](../examples/flutter/windows/runner/tunnel_method_channel.cpp) | Windows Channel 宿主示例 |

---

## 附录：常用命令速查

```powershell
# 开发联调
.\tunnel_server.exe --quic --relay
.\tunnel_client.exe --quic --insecure --relay

# 整机隧道（管理员）
.\tunnel_client.exe --quic --wintun --global-route --endpoint:127.0.0.1 --port:44333 --insecure

# 远程服务器（管理员，无 insecure）
.\tunnel_client.exe --quic --wintun --global-route --endpoint:203.0.113.10 --port:44333

# 生成开发证书
powershell -ExecutionPolicy Bypass -File scripts\gen_dev_cert.ps1

# C# 示例
cd examples\csharp\SecureTunnel.Example && dotnet run
```

---

如有问题，请先运行 `ctest --test-dir build-agent --output-on-failure` 确认核心功能正常，再排查集成层代码。
