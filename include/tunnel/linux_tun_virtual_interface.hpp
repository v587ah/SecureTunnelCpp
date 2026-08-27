#pragma once

#include "tunnel/tunnel_engine.hpp"

#include <cstdint>
#include <string>

namespace tunnel {

// Linux /dev/net/tun 虚拟网卡实现。
// 需要 CAP_NET_ADMIN 或 root 权限；服务端节点部署时使用。
#ifndef _WIN32
class LinuxTunVirtualInterface final : public VirtualInterface {
public:
    explicit LinuxTunVirtualInterface(std::string interface_name = "tun0");

    bool open(std::uint16_t mtu) override;
    Packet read_packet() override;
    bool write_packet(std::span<const std::byte> packet) override;
    void close() noexcept override;

    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    std::string interface_name_;
    std::string last_error_;
    int fd_{-1};
    std::uint16_t mtu_{1280};
};
#endif

}  // namespace tunnel
