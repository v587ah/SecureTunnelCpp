#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include "tunnel/routing_mode.hpp"
#include "tunnel/windows_net_config.hpp"
#endif

#include "tunnel/inner_session.hpp"

namespace tunnel {

// 隧道运行角色：客户端负责接管本机流量，服务端负责解封装并转发。
enum class Role {
    client,
    server,
};

// QUIC/TLS 1.3 传输相关配置（MsQuic + Schannel）。
struct QuicTransportConfig {
    // 应用层协议标识，客户端与服务端必须一致。
    std::string alpn{"sectunnel"};

    // Windows 证书存储中的 SHA1 指纹（40 位十六进制，无分隔符）。
    std::string cert_hash;

    // 可选：PEM 证书/私钥文件路径（开发环境备用）。
    std::string cert_file;
    std::string key_file;

    // 仅允许 localhost 开发联调时跳过服务端证书校验。
    bool insecure{false};

    std::uint64_t idle_timeout_ms{30000};
};

// 项目配置的最小集合。
// 后续可以从 JSON/TOML 读取，但私钥不应直接保存在普通配置文件中。
struct TunnelConfig {
    Role role{Role::client};
    std::string endpoint{"127.0.0.1"};
    std::uint16_t port{443};
    std::uint16_t mtu{1280};

    // 使用 QUIC/TLS 传输时设为 true；开发适配器应保持 false。
    bool prefer_quic{false};

    QuicTransportConfig quic;

    InnerSessionConfig inner;

    // 数据面 relay 参数。
    struct DataPlaneConfig {
        // 每次 relay_batch 最多处理的入站帧数。
        std::size_t batch_size{8};
        // 无数据时的休眠间隔（毫秒）。
        std::uint32_t idle_sleep_ms{1};
        // 缓冲池容量与默认预分配大小。
        std::size_t buffer_pool_capacity{16};
        std::size_t buffer_default_capacity{2048};
    } data_plane;

    // 开发阶段默认禁用 0-RTT，避免控制消息遭受重放攻击。
    bool allow_zero_rtt{false};

#ifdef _WIN32
    // Windows 客户端虚拟网卡地址。默认只配置接口 IP/DNS，不改默认路由。
    Ipv4InterfaceConfig ipv4{
        .address = "10.66.66.2",
        .prefix_length = 24,
        .dns = "1.1.1.1",
        .apply_on_open = true,
    };

    // 全局默认路由必须显式打开；仅在 QUIC 握手完成后由引擎应用。
    bool enable_global_default_route{false};
    ClientRoutingMode client_routing_mode{ClientRoutingMode::none};
    std::string default_route_gateway{"10.66.66.1"};
    std::string smart_route_domain_list{"config/smart_route_domains.txt"};
    std::string smart_route_cidr_list{"config/smart_route_cidrs.txt"};
#endif

    // 检查配置是否符合项目的安全和网络约束。
    [[nodiscard]] bool is_valid(std::string& error) const;
};

}  // namespace tunnel
