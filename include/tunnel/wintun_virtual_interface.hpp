#pragma once

#include "tunnel/tunnel_engine.hpp"
#include "tunnel/smart_route_manager.hpp"
#include "tunnel/windows_net_config.hpp"
#include "tunnel/wintun_loader.hpp"

#include <cstdint>
#include <string>

namespace tunnel {

// Windows 虚拟网卡实现（基于 WireGuard 官方 Wintun）。
//
// 作用：
// 1. 创建/打开虚拟网卡，让操作系统把 IP 包交给本进程。
// 2. 为适配器配置 IPv4/DNS（仅接口级，不劫持默认路由）。
// 3. 从环形缓冲读取上行包、写入下行包。
class WintunVirtualInterface final : public VirtualInterface {
public:
    explicit WintunVirtualInterface(
        std::wstring adapter_name = L"SecureTunnel",
        std::wstring tunnel_type = L"SecureTunnel",
        std::uint32_t ring_capacity = 0x400000,
        Ipv4InterfaceConfig ipv4_config = {});

    ~WintunVirtualInterface() override;

    bool open(std::uint16_t mtu) override;
    Packet read_packet() override;
    bool write_packet(std::span<const std::byte> packet) override;
    void close() noexcept override;
    bool apply_client_routing(
        const TunnelConfig& config,
        std::string& error) override;
    void clear_client_routing() noexcept override;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const std::string& last_error() const noexcept;
    [[nodiscard]] std::uint64_t adapter_luid_value() const noexcept;
    [[nodiscard]] std::uint32_t driver_version() const noexcept;
    [[nodiscard]] const Ipv4InterfaceConfig& ipv4_config() const noexcept;

private:
    bool apply_network_config();
    void clear_network_config() noexcept;
    void clear_client_routes() noexcept;

    std::wstring adapter_name_;
    std::wstring tunnel_type_;
    std::uint32_t ring_capacity_;
    Ipv4InterfaceConfig ipv4_config_;

    std::uint16_t mtu_{0};
    bool network_applied_{false};
    bool client_routes_applied_{false};

    WintunLoader loader_;
    WindowsNetConfigurator net_config_;
    SmartRouteManager smart_route_manager_;
    WINTUN_ADAPTER_HANDLE adapter_{nullptr};
    WINTUN_SESSION_HANDLE session_{nullptr};
    std::string last_error_;
};

}  // namespace tunnel
