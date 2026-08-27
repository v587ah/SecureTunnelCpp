#include "tunnel/development_adapters.hpp"
#include "tunnel/tunnel_engine.hpp"

#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    auto server_iface = std::make_unique<tunnel::DevelopmentVirtualInterface>();
    auto client_iface = std::make_unique<tunnel::DevelopmentVirtualInterface>();
    auto* server_view = server_iface.get();
    auto* client_view = client_iface.get();

    tunnel::TunnelConfig server_config;
    server_config.role = tunnel::Role::server;
    server_config.endpoint = "127.0.0.1";
    server_config.port = 19999;

    tunnel::TunnelConfig client_config;
    client_config.role = tunnel::Role::client;
    client_config.endpoint = "127.0.0.1";
    client_config.port = 19999;

    tunnel::TunnelEngine server{
        server_config,
        std::move(server_iface),
        std::make_unique<tunnel::DevelopmentSecureTransport>()};

    tunnel::TunnelEngine client{
        client_config,
        std::move(client_iface),
        std::make_unique<tunnel::DevelopmentSecureTransport>()};

    if (!expect(server.start(), "服务端启动失败")) {
        return 1;
    }
    if (!expect(client.start(), "客户端启动失败")) {
        return 1;
    }

    client_view->inject_inbound_packet({
        std::byte{0x45},
        std::byte{0x01},
        std::byte{0xBE},
        std::byte{0xEF},
    });

    if (!expect(client.relay_once(), "客户端 relay 失败")) {
        return 1;
    }
    if (!expect(server.relay_once(), "服务端 relay 失败")) {
        return 1;
    }

    const tunnel::Packet received = server_view->read_packet();
    if (!expect(received.size() == 4, "服务端未收到预期长度的 IP 包")) {
        return 1;
    }
    if (!expect(received[0] == std::byte{0x45}, "载荷内容不匹配")) {
        return 1;
    }

    client.stop();
    server.stop();

    std::cout << "data_plane_relay_tests passed\n";
    return 0;
}
