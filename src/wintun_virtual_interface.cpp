#include "tunnel/wintun_virtual_interface.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>
#include <utility>

namespace tunnel {

WintunVirtualInterface::WintunVirtualInterface(
    std::wstring adapter_name,
    std::wstring tunnel_type,
    const std::uint32_t ring_capacity,
    Ipv4InterfaceConfig ipv4_config)
    : adapter_name_(std::move(adapter_name)),
      tunnel_type_(std::move(tunnel_type)),
      ring_capacity_(ring_capacity),
      ipv4_config_(std::move(ipv4_config)),
      smart_route_manager_(net_config_) {}

WintunVirtualInterface::~WintunVirtualInterface() {
    close();
}

bool WintunVirtualInterface::apply_network_config() {
    if (!ipv4_config_.apply_on_open) {
        return true;
    }

    const auto luid = adapter_luid_value();
    if (luid == 0) {
        last_error_ = "无法获取适配器 LUID，跳过网络配置";
        return false;
    }

    if (!net_config_.apply_ipv4(luid, ipv4_config_)) {
        last_error_ = net_config_.last_error();
        return false;
    }

    network_applied_ = true;
    return true;
}

void WintunVirtualInterface::clear_client_routes() noexcept {
    smart_route_manager_.stop();

    if (!client_routes_applied_) {
        return;
    }

    net_config_.clear_client_routes();
    client_routes_applied_ = false;
}

void WintunVirtualInterface::clear_client_routing() noexcept {
    clear_client_routes();
}

bool WintunVirtualInterface::apply_client_routing(
    const TunnelConfig& config,
    std::string& error) {
    if (config.client_routing_mode == ClientRoutingMode::none &&
        !config.enable_global_default_route) {
        error.clear();
        return true;
    }

    if (!is_open()) {
        error = "Wintun 尚未打开，无法应用客户端路由";
        last_error_ = error;
        return false;
    }

    const auto luid = adapter_luid_value();
    if (luid == 0) {
        error = "无法获取适配器 LUID";
        last_error_ = error;
        return false;
    }

    if (!net_config_.apply_endpoint_bypass_route(config.endpoint, luid, error)) {
        last_error_ = error.empty() ? net_config_.last_error() : error;
        return false;
    }

    if (config.client_routing_mode == ClientRoutingMode::global ||
        config.enable_global_default_route) {
        if (!net_config_.apply_global_default_route(
                luid, config.default_route_gateway, error)) {
            net_config_.clear_client_routes();
            last_error_ = error.empty() ? net_config_.last_error() : error;
            return false;
        }
    } else if (config.client_routing_mode == ClientRoutingMode::smart) {
        SmartRouteManager::Options options{
            .tunnel_luid = luid,
            .gateway_ipv4 = config.default_route_gateway,
            .domain_list_path = config.smart_route_domain_list,
            .cidr_list_path = config.smart_route_cidr_list,
        };
        if (!smart_route_manager_.start(options, error)) {
            net_config_.clear_client_routes();
            last_error_ = error;
            return false;
        }
    }

    client_routes_applied_ = true;
    error.clear();
    return true;
}

void WintunVirtualInterface::clear_network_config() noexcept {
    clear_client_routes();
    if (!network_applied_) {
        return;
    }

    const auto luid = adapter_luid_value();
    if (luid != 0) {
        (void)net_config_.clear_ipv4(luid, ipv4_config_);
        if (!ipv4_config_.dns.empty()) {
            (void)net_config_.clear_dns(luid);
        }
    }
    network_applied_ = false;
}

bool WintunVirtualInterface::open(const std::uint16_t mtu) {
    close();

    if (mtu < 1280 || mtu > 1420) {
        last_error_ = "MTU 必须在 1280 到 1420 之间";
        return false;
    }

    if (ring_capacity_ < 0x20000U || ring_capacity_ > 0x4000000U ||
        (ring_capacity_ & (ring_capacity_ - 1U)) != 0U) {
        last_error_ = "Wintun 环形缓冲容量非法";
        return false;
    }

    if (!loader_.load()) {
        last_error_ = loader_.last_error();
        return false;
    }

    adapter_ = loader_.open_adapter(adapter_name_.c_str());
    if (adapter_ == nullptr) {
        adapter_ = loader_.create_adapter(
            adapter_name_.c_str(), tunnel_type_.c_str(), nullptr);
    }

    if (adapter_ == nullptr) {
        last_error_ =
            "创建/打开 Wintun 适配器失败。" + format_win32_error(GetLastError()) +
            " 通常需要以管理员身份运行。";
        loader_.unload();
        return false;
    }

    session_ = loader_.start_session(adapter_, ring_capacity_);
    if (session_ == nullptr) {
        last_error_ =
            "启动 Wintun 会话失败。" + format_win32_error(GetLastError());
        loader_.close_adapter(adapter_);
        adapter_ = nullptr;
        loader_.unload();
        return false;
    }

    mtu_ = mtu;

    if (!apply_network_config()) {
        loader_.end_session(session_);
        session_ = nullptr;
        loader_.close_adapter(adapter_);
        adapter_ = nullptr;
        loader_.unload();
        return false;
    }

    last_error_.clear();
    return true;
}

Packet WintunVirtualInterface::read_packet() {
    if (session_ == nullptr || loader_.receive_packet == nullptr) {
        return {};
    }

    DWORD packet_size = 0;
    BYTE* raw = loader_.receive_packet(session_, &packet_size);
    if (raw == nullptr) {
        const DWORD error = GetLastError();
        if (error != ERROR_NO_MORE_ITEMS) {
            last_error_ = "读取数据包失败。" + format_win32_error(error);
        }
        return {};
    }

    Packet packet(packet_size);
    std::memcpy(packet.data(), raw, packet_size);
    loader_.release_receive_packet(session_, raw);
    return packet;
}

bool WintunVirtualInterface::write_packet(
    const std::span<const std::byte> packet) {
    if (session_ == nullptr || loader_.allocate_send_packet == nullptr ||
        loader_.send_packet == nullptr) {
        return false;
    }

    if (packet.empty() || packet.size() > mtu_) {
        last_error_ = "写入数据包长度非法或超过 MTU";
        return false;
    }

    BYTE* raw = loader_.allocate_send_packet(
        session_, static_cast<DWORD>(packet.size()));
    if (raw == nullptr) {
        last_error_ =
            "分配发送缓冲失败。" + format_win32_error(GetLastError());
        return false;
    }

    std::memcpy(raw, packet.data(), packet.size());
    loader_.send_packet(session_, raw);
    return true;
}

void WintunVirtualInterface::close() noexcept {
    clear_network_config();

    if (session_ != nullptr && loader_.end_session != nullptr) {
        loader_.end_session(session_);
        session_ = nullptr;
    }

    if (adapter_ != nullptr && loader_.close_adapter != nullptr) {
        loader_.close_adapter(adapter_);
        adapter_ = nullptr;
    }

    mtu_ = 0;
    loader_.unload();
}

bool WintunVirtualInterface::is_open() const noexcept {
    return session_ != nullptr && adapter_ != nullptr;
}

const std::string& WintunVirtualInterface::last_error() const noexcept {
    return last_error_;
}

std::uint64_t WintunVirtualInterface::adapter_luid_value() const noexcept {
    if (adapter_ == nullptr || loader_.get_adapter_luid == nullptr) {
        return 0;
    }

    NET_LUID luid{};
    loader_.get_adapter_luid(adapter_, &luid);
    return luid.Value;
}

std::uint32_t WintunVirtualInterface::driver_version() const noexcept {
    return loader_.running_driver_version();
}

const Ipv4InterfaceConfig& WintunVirtualInterface::ipv4_config() const noexcept {
    return ipv4_config_;
}

}  // namespace tunnel
