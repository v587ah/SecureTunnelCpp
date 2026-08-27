#include "tunnel/config.hpp"
#include "tunnel/windows_net_config.hpp"

#include <cassert>
#include <string>

int main() {
    std::uint32_t value = 0;
    std::string error;

    assert(tunnel::WindowsNetConfigurator::parse_ipv4("10.66.66.2", value, error));
    assert(value == 0x0A424202);

    assert(!tunnel::WindowsNetConfigurator::parse_ipv4("999.1.1.1", value, error));
    assert(!error.empty());

    tunnel::WindowsNetConfigurator configurator;
    std::string route_error;
    assert(!configurator.apply_global_default_route(0, "10.66.66.1", route_error));
    assert(!route_error.empty());

    tunnel::TunnelConfig config;
    config.role = tunnel::Role::client;
    config.prefer_quic = false;
    config.enable_global_default_route = true;
    assert(!config.is_valid(error));
    assert(!error.empty());

    config.prefer_quic = true;
    config.quic.alpn = "sectunnel";
    assert(config.is_valid(error));

    return 0;
}
