# 远程对接指南（Windows 客户端 + 服务端）

本文说明如何 **验证 Wintun 网卡**，以及如何将 Windows 客户端接到 **远程服务端**。

> 完整 Linux 部署见 [LINUX_SERVER_DEPLOY.md](LINUX_SERVER_DEPLOY.md)

---

## 一、验证 Wintun 网卡（Windows）

Wintun 在客户端 **退出后会自动删除**，验证时必须 **保持 client 运行**。

### 步骤 1：开两个窗口

**窗口 A — Server（可先开）：**
```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp
.\build-agent\tunnel_server.exe --quic --relay
```

**窗口 B — Client（管理员 PowerShell，保持运行）：**
```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp
.\build-agent\tunnel_client.exe --quic --insecure --wintun --relay
```

### 步骤 2：开第三个管理员窗口跑验证脚本

```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp
powershell -ExecutionPolicy Bypass -File scripts\verify_wintun.ps1
```

### 期望结果

```text
[OK] Adapter found: SecureTunnel (Up)
[OK] IPv4: 10.66.66.2/24
[OK] DNS: 1.1.1.1
[INFO] No default route on SecureTunnel (normal for --wintun without --global-route)
```

### 手动命令（可选）

```powershell
Get-NetAdapter | Where-Object Name -like "*SecureTunnel*"
Get-NetIPAddress -InterfaceAlias "SecureTunnel"
Get-DnsClientServerAddress -InterfaceAlias "SecureTunnel"
route print 0.0.0.0
```

---

## 二、远程对接方案选择

| 方案 | 服务端系统 | QUIC 支持 | 推荐度 |
|------|-----------|-----------|--------|
| **A. Windows VPS** | Windows Server | ✅ 现成可用 | ⭐ 推荐（当前最省事） |
| **B. Linux VPS** | Ubuntu/Debian | ⚠️ 需自行编译 Linux MsQuic | 进阶 |
| **C. 本机双窗口** | 同一台 Windows | ✅ | 仅开发联调 |

> **重要**：当前 CMake **仅在 Windows** 自动启用 MsQuic。Linux 上 `tunnel_server` 默认是 **开发传输**，不能和 Windows QUIC 客户端互通。Linux 生产 QUIC 需先集成 [MsQuic Linux 构建](https://github.com/microsoft/msquic)。

---

## 三、方案 A：Windows VPS 作服务端（推荐）

### 3.1 在 VPS 上

1. 复制整个项目或至少 `build-agent/` 到 VPS
2. 生成/导入 TLS 证书到 `CurrentUser\My`（与本地相同）：
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\gen_dev_cert.ps1
   ```
3. 防火墙放行 **UDP 44333**
4. 启动服务端：
   ```powershell
   .\build-agent\tunnel_server.exe --quic --tun --relay
   ```
   > Windows 服务端 `--tun` 若未完整实现，可先用 `--quic --relay` 测 QUIC 握手。

### 3.2 在本机 Windows 客户端

**远程不能用 `--insecure`**，需信任服务端证书。

#### 导入 VPS 证书到本机（一次性）

将 VPS 上的自签证书导出为 `.cer`，在本机 **管理员 PowerShell**：

```powershell
Import-Certificate -FilePath "C:\path\to\server.cer" -CertStoreLocation Cert:\LocalMachine\Root
```

#### 连接命令（管理员）

```powershell
cd C:\Users\Admin\Desktop\SecureTunnelCpp

# 仅 Wintun，不改默认路由
.\build-agent\tunnel_client.exe --quic --wintun --relay `
  --endpoint:YOUR_VPS_PUBLIC_IP --port:44333

# 整机流量走隧道
.\build-agent\tunnel_client.exe --quic --wintun --global-route `
  --endpoint:YOUR_VPS_PUBLIC_IP --port:44333
```

将 `YOUR_VPS_PUBLIC_IP` 换成 VPS 公网 IP。

---

## 四、方案 B：Linux VPS 作服务端

### 4.1 前置条件

- Ubuntu 22.04+ / Debian 12+
- 公网 **UDP 44333** 已放行（云安全组 + 本机防火墙）
- 已集成 **Linux 版 MsQuic** 并成功编译 `tunnel_server`（带 `TUNNEL_HAS_MSQUIC`）

若尚未集成 MsQuic，可先部署 NAT/TUN 脚本做网络层准备，但 **无法与 Windows QUIC 客户端握手**。

### 4.2 Linux 一键部署流程

```bash
# 1. 上传项目到 VPS
cd SecureTunnelCpp

# 2. 安装依赖
sudo apt update
sudo apt install -y build-essential cmake ninja-build libssl-dev nftables iproute2

# 3. 编译（需已集成 Linux MsQuic 后才有 --quic）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo install -m 0755 build/tunnel_server /usr/local/bin/

# 4. 安装部署脚本
sudo deploy/linux/scripts/install.sh "$(pwd)"

# 5. 编辑配置
sudo nano /etc/sectunnel/sectunnel.env
# 设置 WAN_IF=eth0（或你的公网网卡名）

# 6. 生成 TLS 证书
sudo mkdir -p /etc/sectunnel/certs
sudo openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout /etc/sectunnel/certs/server.key \
  -out /etc/sectunnel/certs/server.crt \
  -days 365 -subj "/CN=your.domain.com"
sudo chmod 600 /etc/sectunnel/certs/server.key

# 7. 启用 NAT
sudo /usr/local/lib/sectunnel/apply-nat.sh

# 8. 启动服务
sudo systemctl enable --now sectunnel-server
sudo systemctl status sectunnel-server
journalctl -u sectunnel-server -f
```

### 4.3 验证 Linux 服务端

```bash
# NAT 规则
sudo nft list table ip sectunnel

# IP 转发
sysctl net.ipv4.ip_forward

# TUN 地址
ip addr show sectun0

# UDP 监听（MsQuic 集成后）
ss -ulnp | grep 44333
```

### 4.4 Windows 客户端连接 Linux

1. 将 Linux 上的 `/etc/sectunnel/certs/server.crt` 复制到 Windows
2. 导入信任：
   ```powershell
   Import-Certificate -FilePath "C:\path\to\server.crt" -CertStoreLocation Cert:\LocalMachine\Root
   ```
3. 连接（管理员）：
   ```powershell
   cd C:\Users\Admin\Desktop\SecureTunnelCpp

   .\build-agent\tunnel_client.exe --quic --wintun --global-route `
     --endpoint:YOUR_LINUX_VPS_IP --port:44333
   ```

---

## 五、网络拓扑速查

```text
[你的 Windows PC]
  Wintun 10.66.66.2
  default route -> 10.66.66.1  (仅 --global-route 时)
       |
       | QUIC/UDP 44333
       v
[远程 VPS 公网 IP]
  tunnel_server
  TUN/网关 10.66.66.1/24
  NAT MASQUERADE -> 公网
```

| 参数 | 值 |
|------|-----|
| 客户端 TUN IP | `10.66.66.2/24` |
| 隧道网关 | `10.66.66.1` |
| QUIC 端口 | UDP `44333` |
| ALPN | `sectunnel` |

---

## 六、常见问题

| 现象 | 处理 |
|------|------|
| 验证脚本找不到网卡 | client 已退出；用 `--wintun --relay` 保持运行 |
| 远程 client 无法 `--insecure` | 正常；导入服务端证书到 `LocalMachine\Root` |
| Linux 客户端连不上 | 确认 Linux 已编译 MsQuic 版 server，不是 dev 传输 |
| 能连上不能上网 | Linux 执行 `apply-nat.sh`；Windows 加 `--global-route` |
| UDP 被墙 | 云厂商安全组放行 44333/udp |

---

## 七、推荐测试顺序

1. ✅ 本机 `--quic --relay`（已完成）
2. ✅ 本机 `--quic --wintun --relay` + `verify_wintun.ps1`
3. 本机 `--quic --wintun --global-route`（整机隧道）
4. 远程 Windows VPS 或 Linux VPS（证书 + `--endpoint`）

相关文档：[USAGE_TUTORIAL.md](USAGE_TUTORIAL.md) · [LINUX_SERVER_DEPLOY.md](LINUX_SERVER_DEPLOY.md)
