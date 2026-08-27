#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tunnel {

// 虚拟网卡上的 IPv4 配置。
// 只描述“这块网卡自己的地址”，不包含默认路由策略。
struct Ipv4InterfaceConfig {
    // 例如 10.66.66.2
    std::string address{"10.66.66.2"};

    // 前缀长度，例如 24 表示 255.255.255.0
    std::uint8_t prefix_length{24};

    // 可选 DNS。为空表示不改系统 DNS。
    std::string dns;

    // 是否在 open() 成功后自动应用；close() 时会尽量清理。
    bool apply_on_open{true};
};

// Windows 网络配置助手。
// 通过适配器 LUID 设置/清理单播地址与接口 DNS。
// 默认不做 0.0.0.0/0 全局路由，避免半成品劫持整机流量。
class WindowsNetConfigurator {
public:
    // 解析点分十进制 IPv4。成功返回 true。
    [[nodiscard]] static bool parse_ipv4(
        const std::string& text,
        std::uint32_t& host_order_address,
        std::string& error);

    // 给指定 LUID 的适配器添加 IPv4 单播地址。
    [[nodiscard]] bool apply_ipv4(
        std::uint64_t luid_value,
        const Ipv4InterfaceConfig& config);

    // 删除本助手此前添加的 IPv4 地址（按地址精确删除）。
    [[nodiscard]] bool clear_ipv4(
        std::uint64_t luid_value,
        const Ipv4InterfaceConfig& config) noexcept;

    // 设置接口级 DNS（仅影响该适配器，不是全局改 Hosts）。
    // Windows 10 1809+ 使用 SetInterfaceDnsSettings。
    [[nodiscard]] bool apply_dns(
        std::uint64_t luid_value,
        const std::string& dns_ipv4);

    // 清除接口 DNS 覆盖。
    [[nodiscard]] bool clear_dns(std::uint64_t luid_value) noexcept;

    // 为远端节点添加 /32 旁路路由，避免 QUIC 流量经隧道环路。
    [[nodiscard]] bool apply_endpoint_bypass_route(
        const std::string& endpoint_host,
        std::uint64_t tunnel_luid_value,
        std::string& error);

    // 将 0.0.0.0/0 默认路由指向隧道网关（需在安全传输就绪后调用）。
    [[nodiscard]] bool apply_global_default_route(
        std::uint64_t luid_value,
        const std::string& gateway_ipv4,
        std::string& error);

    // 为指定前缀添加经隧道网关的路由（智能分流用）。
    [[nodiscard]] bool apply_prefix_route(
        std::uint64_t tunnel_luid_value,
        const std::string& destination_ipv4,
        std::uint8_t prefix_length,
        const std::string& gateway_ipv4,
        std::string& error);

    // 清理本助手添加的旁路、默认与选择性前缀路由。
    void clear_client_routes() noexcept;

    [[nodiscard]] std::size_t selective_route_count() const noexcept;

    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    struct SelectiveRouteKey {
        std::uint64_t tunnel_luid{0};
        std::uint32_t destination_host_order{0};
        std::uint8_t prefix_length{0};
        std::string gateway;

        bool operator==(const SelectiveRouteKey& other) const {
            return tunnel_luid == other.tunnel_luid &&
                   destination_host_order == other.destination_host_order &&
                   prefix_length == other.prefix_length &&
                   gateway == other.gateway;
        }
    };

    std::string last_error_;
    bool bypass_route_applied_{false};
    bool default_route_applied_{false};
    std::string bypass_destination_;
    std::string bypass_gateway_;
    std::uint64_t bypass_interface_luid_{0};
    std::uint64_t default_tunnel_luid_{0};
    std::string default_gateway_;
    std::vector<SelectiveRouteKey> selective_routes_;
};

}  // namespace tunnel
