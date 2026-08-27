# Linux 服务端部署指南

本文说明如何在 Linux 上部署 SecureTunnel **服务端节点**：编译程序、配置 TUN 网卡、启用 **nftables NAT**，并用 systemd 托管 `tunnel_server`。

## 架构概览

```text
客户端 (Windows/Wintun 10.66.66.2)
    │  QUIC/UDP + TLS 1.3 + 内层 AEAD
    ▼
Linux 服务端 (公网 IP:44333)
    │  tunnel_server 解封装
    ▼
TUN sectun0 (10.66.66.1/24)
    │  ip_forward + nftables MASQUERADE
    ▼
公网 (WAN，如 eth0)
```

默认隧道网段与 Windows 客户端一致：

| 角色 | 地址 |
|------|------|
| 客户端 TUN | `10.66.66.2/24` |
| 服务端 TUN 网关 | `10.66.66.1/24` |
| 网段 | `10.66.66.0/24` |

## 前置条件

- **系统**：Ubuntu 22.04+ / Debian 12+ / 其他支持 nftables 的发行版
- **权限**：root 或 sudo（配置 TUN、nftables、systemd）
- **软件**：
  - `build-essential`、`cmake`、`ninja-build`、`git`
  - `nftables`、`iproute2`
  - Linux 上 MsQuic 需 OpenSSL 开发包（见下方编译说明）
- **网络**：
  - 公网 UDP 端口可达（默认 **44333**，可改为 443）
  - 安全组/防火墙放行该 UDP 端口

## 1. 编译服务端

在 Linux 机器或交叉编译环境中：

```bash
git clone <你的仓库> SecureTunnelCpp
cd SecureTunnelCpp

# 依赖（Debian/Ubuntu 示例）
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
    libssl-dev pkg-config

# MsQuic on Linux：需从源码或发行版包安装，详见 MsQuic 官方文档
# 若暂未集成 Linux MsQuic，可先用开发传输联调 NAT/TUN 脚本

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo install -m 0755 build/tunnel_server /usr/local/bin/tunnel_server
```

## 2. 安装部署文件

```bash
cd SecureTunnelCpp
sudo deploy/linux/scripts/install.sh "$(pwd)"
```

将安装：

- `/etc/sectunnel/sectunnel.env` — 环境变量（首次从 example 复制）
- `/usr/local/lib/sectunnel/*.sh` — 运维脚本
- `/etc/systemd/system/sectunnel-server.service` — systemd 单元

## 3. 配置环境变量

编辑 `/etc/sectunnel/sectunnel.env`：

```bash
sudo nano /etc/sectunnel/sectunnel.env
```

关键项：

```bash
TUN_IF=sectun0
TUN_NET=10.66.66.0/24
TUN_GW=10.66.66.1
WAN_IF=eth0          # 留空则 apply-nat.sh 自动检测默认路由网卡
QUIC_PORT=44333
SSH_PORT=22
TUNNEL_SERVER_BIN=/usr/local/bin/tunnel_server
TLS_CERT_FILE=/etc/sectunnel/certs/server.crt
TLS_KEY_FILE=/etc/sectunnel/certs/server.key
```

可选：限制 QUIC 仅来自指定网段：

```bash
QUIC_ALLOW_CIDR=203.0.113.0/24
```

## 4. TLS 证书（Linux / OpenSSL PEM）

Linux 服务端使用 **PEM 证书文件**，而非 Windows 证书存储指纹：

```bash
sudo mkdir -p /etc/sectunnel/certs
sudo openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout /etc/sectunnel/certs/server.key \
    -out /etc/sectunnel/certs/server.crt \
    -days 365 -subj "/CN=sectunnel.example.com"
sudo chmod 600 /etc/sectunnel/certs/server.key
```

生产环境请使用受信任 CA 或内网 PKI 签发的证书。

## 5. 启用 TUN 与 NAT

### 5.1 配置 TUN 地址

```bash
sudo /usr/local/lib/sectunnel/setup-tun.sh up
ip addr show sectun0
```

> **说明**：`tunnel_server --tun` 启动时会创建/打开 TUN；`setup-tun.sh` 在 systemd `ExecStartPre` 中预先配置网关 IP。

### 5.2 加载 nftables 规则

```bash
sudo /usr/local/lib/sectunnel/apply-nat.sh
```

脚本会：

1. 开启 `net.ipv4.ip_forward=1`（并写入 `sysctl.d`）
2. 创建 `table ip sectunnel`，包含：
   - **input**：允许 lo、已建立连接、SSH、QUIC UDP、ICMP ping
   - **forward**：允许 `sectun0 → WAN` 出站；`WAN → sectun0` 仅已建立/相关连接
   - **postrouting**：对源地址 `10.66.66.0/24` 做 **MASQUERADE**

验证：

```bash
sudo nft list table ip sectunnel
sysctl net.ipv4.ip_forward
```

### 5.3 静态规则文件（可选）

若不想用脚本，可参考并修改：

`deploy/linux/nftables/sectunnel.nft`

```bash
# 修改变量后
sudo nft -f deploy/linux/nftables/sectunnel.nft
```

### 5.4 移除 NAT 规则

```bash
sudo /usr/local/lib/sectunnel/remove-nat.sh
sudo /usr/local/lib/sectunnel/setup-tun.sh down
```

## 6. 启动服务端

### 手动运行

```bash
sudo tunnel_server --quic --tun --relay \
    --cert_file:/etc/sectunnel/certs/server.crt \
    --key_file:/etc/sectunnel/certs/server.key
```

参数说明：

| 参数 | 含义 |
|------|------|
| `--quic` | 使用 QUIC/TLS 1.3 传输 |
| `--tun` | 使用 Linux `/dev/net/tun`（需 CAP_NET_ADMIN） |
| `--relay` | 持续运行数据面，直到进程退出 |
| `--cert_file:` / `--key_file:` | TLS 证书与私钥（Linux） |

### systemd 托管

确认 `/etc/sectunnel/sectunnel.env` 与证书就绪后：

```bash
sudo systemctl enable sectunnel-server
sudo systemctl start sectunnel-server
sudo systemctl status sectunnel-server
journalctl -u sectunnel-server -f
```

`ExecStartPre` 会自动执行 `setup-tun.sh up`。建议在 `sectunnel-server.service` 启动前**手动执行一次** `apply-nat.sh`，或将 NAT 规则写入开机脚本 / `networkd` 钩子持久化。

## 7. 客户端对接

Windows 客户端（示例）：

```powershell
# 开发（本机）
.\tunnel_client.exe --quic --wintun --insecure --endpoint:127.0.0.1 --port:44333

# 生产（远程 + 整机流量，管理员）
.\tunnel_client.exe --quic --wintun --global-route --endpoint:<公网IP> --port:44333
```

客户端需指向服务端公网 IP 与 `QUIC_PORT`，虚拟网卡网关设为 `10.66.66.1`。

> 全局默认路由通过 `--global-route` 开启；详见 [USAGE_TUTORIAL.md](USAGE_TUTORIAL.md)。

## 8. 连通性验证

在服务端：

```bash
# NAT 与转发
sudo nft list chain ip sectunnel postrouting
ping -c1 10.66.66.1

# QUIC 端口监听（MsQuic 集成完成后）
ss -ulnp | grep 44333
```

在客户端连接后，于服务端 TUN 抓包：

```bash
sudo tcpdump -i sectun0 -n
```

从客户端 TUN 发起 ping 外网（需客户端已配置路由）：

```text
ping 1.1.1.1  # 经隧道 NAT 出去
```

## 9. 安全建议

1. **防火墙默认拒绝**：`input`/`forward` 链默认 `policy drop`，仅放行必要端口。
2. **限制 QUIC 来源**：生产环境设置 `QUIC_ALLOW_CIDR` 或在上游安全组限源。
3. **不要用 `--insecure`**：客户端必须校验服务端 TLS 证书。
4. **内层 PSK**：通过 `InnerSessionConfig` 配置 AEAD 密钥，不要明文提交到 git。
5. **最小权限**：systemd 单元仅授予 `CAP_NET_ADMIN`；非 root 运行需额外配置 `AmbientCapabilities`。
6. **日志**：勿记录密钥、完整 IP 包内容；`journalctl` 定期轮转。

## 10. 故障排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 客户端 QUIC 握手失败 | UDP 未放行、证书不匹配 | 检查安全组、`TLS_*` 路径、客户端 `--insecure` 仅用于调试 |
| TUN 无地址 | 未运行 setup-tun | `sudo setup-tun.sh up` |
| 能连上但无法上网 | NAT/转发未启用 | `apply-nat.sh`、`net.ipv4.ip_forward=1` |
| `Policy drop` 丢包 | forward 规则不匹配接口名 | 核对 `WAN_IF`、`TUN_IF` 与 `nft list ruleset` |
| systemd 反复重启 | 无证书或 MsQuic 未链接 | `journalctl -u sectunnel-server -e` |

## 11. 文件清单

```text
deploy/linux/
├── sectunnel.env.example      # 配置模板
├── nftables/
│   └── sectunnel.nft          # 静态 nftables 示例
├── scripts/
│   ├── common.sh
│   ├── setup-tun.sh           # TUN IP up/down
│   ├── apply-nat.sh           # 启用 NAT（推荐）
│   ├── remove-nat.sh          # 移除 NAT
│   └── install.sh             # 安装到系统
└── systemd/
    └── sectunnel-server.service
```

## 12. 持久化 nftables（可选）

Debian/Ubuntu 可启用 nftables 服务保存规则：

```bash
sudo nft list ruleset | sudo tee /etc/nftables.conf
sudo systemctl enable nftables
```

注意：与 `apply-nat.sh` 动态生成的 `sectunnel` 表合并时避免重复定义。

---

相关文档：[ARCHITECTURE.md](ARCHITECTURE.md)、[USAGE_TUTORIAL.md](USAGE_TUTORIAL.md)
