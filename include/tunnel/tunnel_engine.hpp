#pragma once

#include "tunnel/config.hpp"
#include "tunnel/packet_buffer_pool.hpp"
#include "tunnel/relay_stats.hpp"
#include "tunnel/session.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace tunnel {

class InnerSession;

// 虚拟网卡收发的原始 IPv4/IPv6 数据包。
using Packet = std::vector<std::byte>;

// 虚拟网卡抽象。
// Windows 实现会使用 Wintun，Linux 实现会使用 /dev/net/tun。
class VirtualInterface {
public:
    virtual ~VirtualInterface() = default;
    virtual bool open(std::uint16_t mtu) = 0;
    virtual Packet read_packet() = 0;
    virtual bool write_packet(std::span<const std::byte> packet) = 0;
    virtual void close() noexcept = 0;

    // 在安全传输就绪后应用客户端路由策略（如全局默认路由）。
    [[nodiscard]] virtual bool apply_client_routing(
        const TunnelConfig& config,
        std::string& error) {
#ifdef _WIN32
        if (config.client_routing_mode == ClientRoutingMode::global ||
            config.enable_global_default_route) {
            error = "当前虚拟网卡实现不支持全局默认路由";
            return false;
        }
        if (config.client_routing_mode == ClientRoutingMode::smart) {
            error = "当前虚拟网卡实现不支持智能分流";
            return false;
        }
#endif
        error.clear();
        return true;
    }

    virtual void clear_client_routing() noexcept {}
};

// 加密通道抽象。
// 生产实现必须接入经过审计的 WireGuard/Noise/TLS 库，不能手写密码算法。
class SecureTransport {
public:
    virtual ~SecureTransport() = default;
    virtual bool connect(const TunnelConfig& config) = 0;
    virtual bool perform_handshake() = 0;

    // 在 TLS/QUIC 握手完成后打开数据通道（如 QUIC 双向流）。
    virtual bool open_data_channel() { return true; }

    // 发送/接收已封装的内层帧（长度前缀由具体传输实现负责）。
    virtual bool send_payload(std::span<const std::byte> payload) = 0;
    [[nodiscard]] virtual std::optional<Packet> try_receive_payload() = 0;

    virtual void close() noexcept = 0;
};

// 隧道引擎只负责协调各模块，不直接实现网卡、密码算法或 QUIC。
// 这种依赖反转便于分别测试，并降低高权限代码的规模。
class TunnelEngine {
public:
    TunnelEngine(
        TunnelConfig config,
        std::unique_ptr<VirtualInterface> virtual_interface,
        std::unique_ptr<SecureTransport> transport,
        std::unique_ptr<InnerSession> inner_session = nullptr);

    [[nodiscard]] bool start();
    void stop() noexcept;
    void request_data_plane_stop() noexcept;
    void reset_server_session() noexcept;

    // 执行一次数据面转发：网卡 → 内层加密 → 传输 → 解密 → 网卡。
    [[nodiscard]] bool relay_once();

    // 批量 relay：一次出站 + 最多 batch_size 个入站帧。
    [[nodiscard]] bool relay_batch(std::size_t batch_size);

    // 在已建立连接上运行数据面循环，直到超时或被 stop() 打断。
    void run_data_plane_for(std::chrono::milliseconds duration);

    [[nodiscard]] const Session& session() const noexcept;
    [[nodiscard]] const RelayStats& relay_stats() const noexcept;

private:
    [[nodiscard]] bool ensure_session_ready();
    [[nodiscard]] bool forward_outbound();
    [[nodiscard]] std::size_t forward_inbound_batch(std::size_t max_packets);

    TunnelConfig config_;
    Session session_;
    std::unique_ptr<VirtualInterface> virtual_interface_;
    std::unique_ptr<SecureTransport> transport_;
    std::unique_ptr<InnerSession> inner_session_;
    PacketBufferPool buffer_pool_;
    RelayStats relay_stats_;
    Packet scratch_sealed_;
    Packet scratch_opened_;
    bool data_plane_stop_requested_{false};
};

}  // namespace tunnel
