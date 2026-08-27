#include "tunnel/config.hpp"

#ifdef _WIN32
#include "tunnel/windows_net_config.hpp"
#endif

namespace tunnel {

bool TunnelConfig::is_valid(std::string& error) const {
    if (endpoint.empty()) {
        error = "节点地址不能为空";
        return false;
    }

    if (port == 0) {
        error = "端口必须在 1 到 65535 之间";
        return false;
    }

    // IPv6 要求链路 MTU 至少为 1280；过大的隧道 MTU 又容易触发外层分片。
    if (mtu < 1280 || mtu > 1420) {
        error = "隧道 MTU 建议保持在 1280 到 1420 之间";
        return false;
    }

    if (allow_zero_rtt) {
        error = "当前版本尚未实现 0-RTT 防重放策略，因此禁止启用";
        return false;
    }

    if (prefer_quic) {
        if (quic.alpn.empty()) {
            error = "QUIC ALPN 不能为空";
            return false;
        }

        if (quic.insecure) {
            if (role != Role::client) {
                error = "insecure 仅允许客户端在本地开发联调时使用";
                return false;
            }
            if (endpoint != "127.0.0.1" && endpoint != "localhost") {
                error = "insecure 仅允许连接 localhost/127.0.0.1";
                return false;
            }
        }

        if (role == Role::server) {
            const bool has_hash = !quic.cert_hash.empty();
            const bool has_files =
                !quic.cert_file.empty() && !quic.key_file.empty();
            if (!has_hash && !has_files) {
                error = "服务端启用 QUIC 时必须提供 cert_hash 或 cert_file/key_file";
                return false;
            }
        }
    }

#ifdef _WIN32
    if (enable_global_default_route &&
        client_routing_mode != ClientRoutingMode::none &&
        client_routing_mode != ClientRoutingMode::global) {
        error = "enable_global_default_route 与 client_routing_mode 冲突";
        return false;
    }

    const bool routing_active =
        enable_global_default_route ||
        client_routing_mode == ClientRoutingMode::smart ||
        client_routing_mode == ClientRoutingMode::global;

    if (routing_active) {
        if (role != Role::client) {
            error = "客户端路由策略仅允许客户端启用";
            return false;
        }
        if (!prefer_quic) {
            error = "客户端路由策略要求启用 QUIC/TLS 安全传输（--quic）";
            return false;
        }
        std::uint32_t ignored = 0;
        if (!WindowsNetConfigurator::parse_ipv4(
                default_route_gateway, ignored, error)) {
            return false;
        }
    }

    if (client_routing_mode == ClientRoutingMode::smart) {
        if (smart_route_domain_list.empty() && smart_route_cidr_list.empty()) {
            error = "智能分流至少需要域名列表或 CIDR 列表";
            return false;
        }
    }

    if (ipv4.apply_on_open) {
        std::uint32_t ignored = 0;
        if (!WindowsNetConfigurator::parse_ipv4(ipv4.address, ignored, error)) {
            return false;
        }
        if (ipv4.prefix_length == 0 || ipv4.prefix_length > 32) {
            error = "IPv4 前缀长度必须在 1 到 32 之间";
            return false;
        }
        if (!ipv4.dns.empty() &&
            !WindowsNetConfigurator::parse_ipv4(ipv4.dns, ignored, error)) {
            return false;
        }
    }
#endif

    error.clear();
    return true;
}

}  // namespace tunnel
