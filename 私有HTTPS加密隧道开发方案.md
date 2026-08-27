# 私有 HTTPS 加密隧道开发方案

> 目标：自研一条 **延迟低、吞吐高、加密强** 的私有隧道。  
> 传输层走 **标准 TLS 1.3 / HTTPS / HTTP/3**
> 本文是架构与技术选型文档。

---

## 1. 先定目标，再选协议

一条“比常见商用客户端更强”的隧道，通常要同时满足四件事：

| 目标 | 含义 | 错误做法 |
|---|---|---|
| 延迟低 | 握手短、RTT 少、不队头阻塞 | 把所有流量塞进一条 TCP，再套一层 OpenVPN |
| 网络快 | 吞吐高、丢包时不崩 | 自研拥塞控制、不用成熟 UDP/QUIC |
| 安全 | 前向保密、防重放、防中间人 | 自己发明加密算法 |
| 加密外观正常 | 外层就是真 TLS/HTTPS | 伪造特定网站指纹、对抗审查设备 |

**正确方向：**  
外层用真正的 TLS 1.3（或 HTTP/3/QUIC），内层用现代 AEAD 隧道（WireGuard / Noise）。  
这样密码学是标准的，包形也是标准 HTTPS，不必碰任何闭源私有协议。

不要做的事：

- 反编译、兼容、仿制 Falcon 或其他闭源客户端
- 自己设计块加密、自己拼握手
- 为了“更像某个网站”去伪造浏览器 TLS 指纹、假 SNI

---

## 2. 总体架构

推荐 **双层**，职责分开：

```
设备 App（Flutter / Qt / WPF，只做 UI）
        │  FFI / gRPC
        ▼
客户端引擎（Go 或 C++）
        │  1. 从虚拟网卡收 IP 包
        │  2. 内层加密（Noise / WireGuard）
        │  3. 外层 TLS 1.3 或 QUIC 发出
        ▼
公网（看起来就是 HTTPS / HTTP3）
        ▼
服务端引擎
        │  解开 TLS → 解开内层 → NAT 出网
        ▼
目标网站 / 互联网
```

三层不要混在一个函数里：

1. **接入层**：虚拟网卡（本机流量怎么进来）
2. **隧道层**：加密、鉴权、多路复用、拥塞控制
3. **伪装/传输层**：标准 TLS / HTTPS / QUIC（让外层就是正常加密 Web 流量）

Let’s VPN 流畅，核心也是这个结构：本机 TUN/TAP + 用户态转发 + 加密通道。  
你要更强，是把 **隧道层和传输层换成公开、可审计的现代方案**，而不是抄它的闭源握手。

---

## 3. 推荐技术栈

### 3.1 语言怎么选

| 部分 | 推荐 | 原因 |
|---|---|---|
| 隧道引擎 | **Go** 或 **C++20** | 网络、并发、落地快；C++ 更可控延迟 |
| 加密原语 | 不要手写，调用库 | 见 3.3 |
| 客户端 UI | Flutter / Qt | 和引擎用 FFI 分开 |
| 服务端 | Go + Linux | 部署、协程、运维都成熟 |
| iOS 系统隧道 | Swift Network Extension | 系统强制，不能只靠 Dart/C++ |

若你坚持 **C/C++ 引擎**：

- Windows：Wintun + IOCP / Boost.Asio
- Linux：`/dev/net/tun` + epoll / io_uring
- TLS：BoringSSL 或 OpenSSL 3（只开 TLS 1.3）
- QUIC：MsQuic 或 quiche（Cloudflare）

若更看重开发速度：**Go + quic-go + wireguard-go / noise**。  
性能敏感路径以后再用 C++ 重写热点即可。

### 3.2 传输层（外层，决定“像不像 HTTPS”）

按优先级：

1. **首选：HTTP/3 over QUIC（TLS 1.3）**
   - UDP，无 TCP 队头阻塞
   - 0-RTT 要慎用（见安全章节）
   - 延迟和弱网表现通常最好
   - 外层就是现代浏览器访问 HTTPS 的样子

2. **次选：TLS 1.3 + HTTP/2 多路复用（TCP 443）**
   - 防火墙只放行 TCP/443 时更稳
   - 用 HTTP/2 stream 承载多条逻辑连接，避免“一条 TCP 卡死所有流”
   - 必须开 **BBR** 或至少 Cubic，并做 TLS 会话恢复（session ticket 要设计好）

3. **不要当主力：裸 TCP 自研包格式**
   - 延迟、队头阻塞、中间盒干扰都更差

端口建议：**443**。证书用 **Let’s Encrypt 或自有 CA + 真实域名**，不要用自签证书对着公网（容易被中间人工具和策略识别为异常 TLS）。

### 3.3 加密与密钥交换（内层，决定“够不够安全”）

外层 TLS 已经加密一次。内层再做一次，是为了：

- 服务端入口被拆 TLS 时，内层仍不可读
- 节点之间可以换传输，会话密钥不跟着丢
- 实现真正的 **前向保密** 和 **身份绑定**

| 用途 | 选型 | 说明 |
|---|---|---|
| 密钥交换 | **X25519** | 快、标准、实现多 |
| 后量子（可选加强） | **X25519 + ML-KEM-768 混合** | 抗未来量子计算机，握手会略增 |
| 对称加密 | **ChaCha20-Poly1305** | 移动端/无 AES-NI 时更快；延迟稳定 |
| 对称加密（备选） | **AES-256-GCM** | 服务器 AES-NI 上吞吐更好，可按 CPU 自动选 |
| 哈希 / KDF | **BLAKE2s / HKDF-SHA256** | 不要用 MD5/SHA1 |
| 握手框架 | **Noise（IK 或 XX）** 或直接 **WireGuard** | 不要自绘握手状态机 |
| 身份 | 节点长期公钥 + 预共享密钥（PSK） | PSK 可抗中间人，即使证书被错误信任 |

安全底线：

- 只用 AEAD（带完整性），禁止裸 CBC、禁止 RC4
- 每个方向独立密钥、独立 nonce，**nonce 绝不重复**
- 重放窗口（anti-replay）必须有
- 长期私钥只放服务端，客户端只持用户凭证和服务器公钥指纹

**不要自己写 ChaCha、AES、椭圆曲线。**  
C/C++ 用：libsodium、BoringSSL、libsodium + pqcrypto。  
Go 用：`golang.org/x/crypto`、`cloudflare/circl`（后量子）。

### 3.4 本机接入（流量怎么进隧道）

| 平台 | 技术 | 备注 |
|---|---|---|
| Windows | **Wintun**（优先）或 TAP | Wintun 更快；TAP 是兼容备选 |
| Linux | TUN（`IFF_TUN`） | 服务端也在这里做 NAT |
| Android | `VpnService` | 系统 API |
| iOS / macOS | Network Extension / utun | 必须原生代码 |

用户态协议栈（可选）：

- 简单 VPN：**把 IP 包直接塞进内层隧道**（最接近 WireGuard，延迟最低）
- 需要精细分流 / 假 DNS：再考虑 **gVisor netstack** 或 lwIP  
  不是一开始就上，会增加延迟和复杂度。

### 3.5 服务端与运维

- 系统：Linux + `nftables` / `iptables` NAT
- 转发：内核 IP Forward，或自己做用户态 NAT（控制力强、延迟略差）
- 证书：acme.sh / cert-manager
- 观测：Prometheus（RTT、丢包、握手失败、单连接吞吐）
- 节点下发：你自己的 API（登录、设备数、节点列表）——这是控制面，不要和数据面混用同一条阻塞连接

---

## 4. 低延迟、快网络：必须做的设计

### 4.1 路径尽量短

- **内层用 UDP 语义的数据包隧道**（WireGuard / Noise 数据包），不要把每个 TCP 再封装成一条新 TCP（TCP-over-TCP 会放大延迟和抖动）。
- 外层若必须走 TCP，用 **HTTP/2 多路复用**；能走 QUIC 就走 QUIC。
- 客户端到节点选 **地理近、ASN 质量好** 的入口，比再加密一层更影响手感。

### 4.2 减少握手往返

| 阶段 | 做法 |
|---|---|
| TLS | 只 TLS 1.3；会话恢复用 ticket（注意 0-RTT 风险） |
| 内层 | Noise 1-RTT；预共享静态密钥可压到更少往返 |
| DNS | 客户端缓存节点 IP；避免每次先解析再连 |
| 连接复用 | 一条 QUIC/H2 连接上开多 stream，不要每请求新握手 |

### 4.3 拥塞与丢包

- QUIC / 内层 UDP：让库的拥塞控制工作，不要再套一层自研窗口。
- 开启 **BBR**（Linux `net.ipv4.tcp_congestion_control=bbr`，QUIC 实现也选 BBR 类）。
- 合理 MTU：外层 TLS/QUIC 有开销，内层 MTU 建议 **1280–1380**，避免分片。
- 不要为了“更像网页”把数据包切得很碎——那会明显变慢。

### 4.4 加解密性能

- 有 AES-NI 的服务器：AES-256-GCM
- 手机、小路由、老 PC：ChaCha20-Poly1305
- 运行时按 CPU 能力选，不必全球一种算法
- 避免每包都做系统调用：批量读写 TUN（Wintun ring / `readv`）

### 4.5 分流（体感会“更快”）

全局把微信、支付、局域网也送出国，会又慢又不稳定。  
做 **分流**：

- 国内 / 局域网直连
- 仅指定网段或域名走隧道

这是产品层优化，往往比再换一种密码学更影响“快不快”。

---

## 5. 安全设计清单

### 5.1 必须具备

- [ ] TLS 1.3 only，关闭 1.2 及以下
- [ ] 证书链校验 + **证书锁定（SPKI pin）** 或服务器 Noise 静态公钥锁定
- [ ] 内层 AEAD + 重放保护
- [ ] 前向保密（短期会话密钥，定期轮换，建议 2–8 分钟或按流量）
- [ ] 用户鉴权与数据面分离（JWT/设备密钥只用于拉配置，不把密码当会话密钥）
- [ ] 日志不记 payload、不记完整浏览 URL；只记必要的连接元数据
- [ ] 内存中密钥用完即清（C/C++ 尤其注意）

### 5.2 明确不要开的“加速”

- TLS 1.3 **0-RTT 应用数据**：有重放风险，隧道控制指令禁止走 0-RTT
- 压缩隧道内 HTTP：CRIME/BREACH 类风险，且对已加密数据几乎无收益
- 自定义“更强加密”但缩短 nonce、复用 IV

### 5.3 比闭源商用客户端更强的点（你可以真正做到）

闭源客户端无法被你审计。你的优势应是：

1. 协议公开或至少可自审（Noise / WireGuard + 标准 TLS）
2. 可选 **后量子混合密钥交换**
3. 客户端固定服务器公钥，不盲信系统 CA
4. 开源或至少第三方审计加密与握手
5. 最小权限：引擎独立进程，UI 不碰原始密钥

这些比“再套一层不知名算法”安全得多。

---

## 6. 推荐的协议组合（直接可落地）

### 方案 A：最快（优先推荐）

```
TUN/Wintun
  → WireGuard（ChaCha20-Poly1305 / AES-GCM）
    → QUIC/HTTP3（TLS 1.3，UDP/443）
      → 服务端 wg + nftables NAT
```

- 延迟：通常最好  
- 外观：HTTP/3  
- 工作量：中等（要处理 QUIC 库和证书）

### 方案 B：最稳（TCP 443 环境）

```
TUN/Wintun
  → Noise IK + AEAD 帧
    → TLS 1.3 + HTTP/2（TCP/443，真实证书）
      → 服务端多路复用拆帧 + NAT
```

- 适合只放行 TCP 的网络  
- 注意避免 TCP-over-TCP：内层应是 **数据报帧**，不要再对每个用户 TCP 建一条 TCP

### 方案 C：实现最简单

```
标准 WireGuard UDP
```

- 最快做出来、延迟也很好  
- 外层不是 HTTPS；在只允许 443/TLS 的网络里可能不如 A/B  
- 适合先打通引擎和虚拟网卡，再加外层 TLS/QUIC

**建议开发顺序：先 C，再 A 或 B。** 不要第一步就做“全套伪装 + 后量子 + 自研协议栈”。

---

## 7. 模块划分（C/C++ 引擎示例）

```
etun/
  tun/          虚拟网卡读写（Wintun / TUN）
  crypto/       仅封装 libsodium / BoringSSL，禁止自写算法
  handshake/    Noise 或 WireGuard 握手状态
  transport/    QUIC 或 TLS1.3+H2
  mux/          多路复用、流控、反重放
  auth/         设备密钥、令牌（控制面）
  config/       节点、证书钉扎、MTU
  server/       拆封装、NAT、限速
```

进程模型：

- `etun-core`：高权限，只负责网卡和隧道（Windows 可 SYSTEM 服务）
- App：低权限，只发“连接/断开/选节点”

---

## 8. 开发阶段（建议 6 步）

1. **单机实验室**  
   Linux 两台虚拟机，先跑通 WireGuard，测 RTT 和 iperf3。没有这一步，后面全是盲调。

2. **自建握手小隧道（可选学习）**  
   X25519 + ChaCha20-Poly1305 + 长度帧，只转发一条 TCP。  
   用于理解 AEAD 和 nonce，**不要作为生产协议**。生产用 Noise/WireGuard。

3. **接上 TUN/Wintun**  
   系统流量进引擎，内层仍用 WireGuard。先追求正确，再追求快。

4. **外层套 TLS 1.3 或 QUIC**  
   真实域名 + 有效证书 + 443。用 Wireshark 确认外层是标准 TLS，而不是自创包头。

5. **性能**  
   调 MTU、批量读写、BBR、AES/ChaCha 自动选择、连接复用。用 iperf3 / 真实网页测，不要只看“加密位数”。

6. **安全加固**  
   公钥钉扎、密钥轮换、后量子混合、审计日志、独立权限进程。

每一步都要有可测指标：握手耗时、稳定 RTT、丢包率、单流 Mbps。

---

## 9. 技术栈一览（汇总）

**客户端引擎**

- 语言：Go 或 C++20  
- 网卡：Wintun / TUN / VpnService / Network Extension  
- 内层：WireGuard 或 Noise + ChaCha20-Poly1305 / AES-256-GCM  
- 外层：MsQuic / quiche / quic-go，或 BoringSSL TLS 1.3 + HTTP/2  
- 后量子（可选）：ML-KEM-768 混合 X25519  

**服务端**

- Linux + nftables  
- 同一套引擎的 server 模式  
- Let’s Encrypt  
- Prometheus + Grafana  

**控制面（独立）**

- HTTPS API：登录、设备绑定、节点列表  
- 不要把控制面明文塞进未完成握手的数据通道  

**明确不用**

- 自研加密算法、自研 TLS  
- Falcon / 其他闭源协议兼容  
- OpenVPN + AES-CBC 作为新项目的默认方案  
- PPTP / L2TP 作为安全方案  

---

## 10. 和“更强、更安全”相关的判断标准

可以用下面几条判断你有没有做对，而不是看营销词：

1. 密码学全是标准算法和标准握手（可指出 RFC / Noise 文档）。
2. 有前向保密和重放保护，密钥会轮换。
3. 客户端能钉扎服务器身份，不单靠系统 CA。
4. 内层是数据包隧道，没有 TCP-over-TCP。
5. 外层是真 TLS 1.3 或 HTTP/3，证书有效。
6. 能用 iperf3 和 ping 证明延迟、吞吐，而不是只强调“军用级”。

---

## 11. 结论

- **引擎**：Go 或 C++，TUN/Wintun 收包。  
- **安全**：Noise/WireGuard + X25519 + ChaCha20-Poly1305/AES-GCM，可选后量子混合。  
- **速度**：优先 QUIC/HTTP3；不行再 TLS1.3+HTTP/2；MTU、BBR、多路复用、分流。  
- **HTTPS**：用真正的 TLS/证书/443 承载隧道，而不是仿制闭源“伪装协议”。

下一步若要落地代码：先在两台机器上打通 **方案 C（纯 WireGuard）**，再加 **方案 A 的 QUIC 外层**。不要从“完整私有伪装协议”一次性开工。
