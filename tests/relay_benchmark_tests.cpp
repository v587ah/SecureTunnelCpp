#include "tunnel/development_adapters.hpp"
#include "tunnel/tunnel_engine.hpp"

#include <chrono>
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
    auto* client_view = client_iface.get();

    tunnel::TunnelConfig server_config;
    server_config.role = tunnel::Role::server;
    server_config.endpoint = "127.0.0.1";
    server_config.port = 19998;
    server_config.data_plane.batch_size = 16;

    tunnel::TunnelConfig client_config = server_config;
    client_config.role = tunnel::Role::client;

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

    constexpr std::size_t kPacketCount = 500;
    constexpr std::size_t kPacketSize = 512;

    const auto start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < kPacketCount; ++i) {
        tunnel::Packet packet(kPacketSize);
        packet[0] = static_cast<std::byte>(0x45);
        packet[1] = static_cast<std::byte>(i & 0xFF);

        client_view->inject_inbound_packet(packet);
        if (!client.relay_batch(client_config.data_plane.batch_size)) {
            std::cerr << "FAIL: 客户端 relay 在第 " << i << " 包失败\n";
            return 1;
        }
        if (!server.relay_batch(server_config.data_plane.batch_size)) {
            std::cerr << "FAIL: 服务端 relay 在第 " << i << " 包失败\n";
            return 1;
        }
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    const auto& client_stats = client.relay_stats();
    const auto& server_stats = server.relay_stats();

    if (!expect(client_stats.outbound_packets == kPacketCount, "客户端出站计数不匹配")) {
        return 1;
    }
    if (!expect(server_stats.inbound_packets == kPacketCount, "服务端入站计数不匹配")) {
        return 1;
    }

    const double packets_per_sec =
        elapsed_ms > 0
            ? (static_cast<double>(kPacketCount) * 1000.0) /
                  static_cast<double>(elapsed_ms)
            : 0.0;

    std::cout << "relay_benchmark_tests passed\n";
    std::cout << "  packets=" << kPacketCount
              << " size=" << kPacketSize
              << " elapsed_ms=" << elapsed_ms
              << " pps=" << packets_per_sec << '\n';

    client.stop();
    server.stop();
    return 0;
}
