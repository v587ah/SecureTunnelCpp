# 架构设计

## 数据路径

```text
系统 IP 包
  → VirtualInterface（Wintun/TUN）
  → TunnelEngine
  → 内层安全会话（WireGuard/Noise）
  → QUIC + TLS 1.3（UDP/443）
  → Linux 服务端
  → 解封装、路由、NAT
  → 公网
```

外层使用真实的 QUIC/TLS，而不是模仿某个第三方网站。内层安全会话用于绑定节点身份、提供前向保密和防重放。

## 依赖方向

`TunnelEngine` 只依赖抽象接口，不依赖 Wintun、MsQuic 或 Linux API。平台模块实现接口并注入引擎：

```text
TunnelEngine
  ├─ VirtualInterface
  │    ├─ WintunVirtualInterface
  │    └─ LinuxTunVirtualInterface
  └─ SecureTransport
       ├─ QuicSecureTransport
       └─ DevelopmentSecureTransport（仅测试）
```

这能避免平台代码、网络代码与状态机互相耦合，也方便用假实现测试失败恢复。

## 计划采用的技术栈

### Windows 客户端

- C++20
- Wintun：虚拟网络接口
- MsQuic：QUIC、TLS 1.3、拥塞控制
- Windows Service：高权限核心服务
- Named Pipe：低权限 UI 与核心服务通信
- CMake + vcpkg：构建和依赖管理

### Linux 服务端

- C++20
- `/dev/net/tun`
- MsQuic 或同协议 QUIC 实现
- nftables：NAT 与防火墙
- systemd：进程托管

### 密码学

- 首选 WireGuard/Noise 的成熟实现
- X25519 密钥交换
- ChaCha20-Poly1305 或 AES-256-GCM
- HKDF-SHA-256
- 操作系统 CSPRNG

这些名称是选型，不代表要自行实现算法。

## 线程模型（计划）

第一版采用单事件循环加有限工作线程：

- IO 线程：Wintun ring 和 MsQuic 回调。
- 加密工作线程：只在性能分析证明需要时引入。
- 控制线程：配置、状态、重连。

避免“每条连接一个线程”。跨线程传递数据时使用有界队列，队列满必须背压或丢弃低优先级数据，不能无限占用内存。

## 数据帧（待标准实现确定）

在选定 WireGuard/Noise 实现前，不定义自制加密帧格式。控制消息至少需要：

- 协议版本
- 会话标识
- 单调递增序号
- 消息类型
- 经过 AEAD 认证的载荷

解析器必须先校验长度，再分配内存，并为单帧和单连接设置硬上限。

## 性能指标

不能只以“能连接”作为完成标准。每个版本记录：

- 首次连接和恢复连接耗时
- 稳态 RTT 与 P95/P99 抖动
- 单流和多流吞吐
- 0.1%、1%、3% 丢包下吞吐
- CPU、内存、每包分配次数
- 不同 MTU 下的分片情况

## 里程碑

### M0：工程骨架（已完成）

- 配置约束
- 会话状态机
- 接口与开发适配器
- 基础单元测试

### M1：真实虚拟网卡（进行中）

- Windows Wintun 动态加载
- 适配器创建/打开与会话生命周期
- IP 包读写接口
- **接口级 IPv4/DNS 配置**（`WindowsNetConfigurator`）
- **默认不启用** 0.0.0.0/0 全局路由
- close() 时清理已添加的 IP/DNS

### M2：真实安全传输

- MsQuic 客户端/服务端
- TLS 1.3 证书校验
- 节点公钥或 SPKI 钉扎
- 握手失败和退避重连

### M3：数据面

- 成熟 WireGuard/Noise 会话
- 防重放与密钥轮换
- MTU 探测
- Linux TUN 与 NAT

### M4：性能与加固

- 批量读写、缓冲池、少拷贝
- 弱网和故障注入
- 模糊测试解析器
- 第三方安全审计

## 威胁模型

需要防御：

- 公网窃听和篡改
- 中间人
- 数据包重放
- 旧会话迟到包
- 恶意超长帧导致内存耗尽
- DNS 或路由恢复失败导致流量泄漏

不承诺：

- 隐藏端点 IP
- 对抗终端已经被完全控制
- 通过“私有算法”获得安全性
