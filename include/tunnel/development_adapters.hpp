#pragma once

#include "tunnel/tunnel_engine.hpp"

#include <memory>
#include <optional>

namespace tunnel {

// 仅用于验证工程结构的空网卡。
// 它不会修改系统路由，也不会读取真实网络数据。
class DevelopmentVirtualInterface final : public VirtualInterface {
public:
    bool open(std::uint16_t mtu) override;
    Packet read_packet() override;
    bool write_packet(std::span<const std::byte> packet) override;
    void close() noexcept override;

    // 测试辅助：模拟网卡收到一个入站包。
    void inject_inbound_packet(Packet packet);

private:
    bool opened_{false};
    Packet packet_;
};

// 开发传输：同进程内通过共享队列模拟客户端/服务端收发。
class DevelopmentSecureTransport final : public SecureTransport {
public:
    DevelopmentSecureTransport() = default;
    ~DevelopmentSecureTransport() override;

    bool connect(const TunnelConfig& config) override;
    bool perform_handshake() override;
    bool open_data_channel() override;
    bool send_payload(std::span<const std::byte> payload) override;
    std::optional<Packet> try_receive_payload() override;
    void close() noexcept override;

private:
    TunnelConfig config_{};
    bool connected_{false};
    bool data_channel_open_{false};
    void* link_{nullptr};  // DevelopmentTransportLink*
};

}  // namespace tunnel
