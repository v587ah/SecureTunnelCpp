#include "tunnel/config.hpp"
#include "tunnel/smart_route_manager.hpp"

#include <cassert>
#include <string>

int main() {
    std::string error;
    std::string network;
    std::uint8_t prefix = 0;

    assert(tunnel::parse_cidr_prefix("142.250.0.0/15", network, prefix, error));
    assert(network == "142.250.0.0");
    assert(prefix == 15);

    assert(tunnel::parse_cidr_prefix("10.66.66.2", network, prefix, error));
    assert(network == "10.66.66.2");
    assert(prefix == 32);

    assert(!tunnel::parse_cidr_prefix("bad/99", network, prefix, error));

    const auto domains = tunnel::load_route_domain_list(
        "config/smart_route_domains.txt", error);
    assert(error.empty());
    assert(!domains.empty());

    const auto cidrs = tunnel::load_route_cidr_list(
        "config/smart_route_cidrs.txt", error);
    assert(error.empty());
    assert(!cidrs.empty());

    tunnel::TunnelConfig config;
    config.role = tunnel::Role::client;
    config.prefer_quic = true;
    config.quic.alpn = "sectunnel";
    config.client_routing_mode = tunnel::ClientRoutingMode::smart;
    assert(config.is_valid(error));

    config.client_routing_mode = tunnel::ClientRoutingMode::global;
    config.enable_global_default_route = true;
    assert(config.is_valid(error));

    return 0;
}
