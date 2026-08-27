#include "tunnel/development_adapters.hpp"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace tunnel {
namespace {

struct DevelopmentTransportLink {
    std::mutex mutex;
    std::deque<Packet> to_server;
    std::deque<Packet> to_client;
};

std::mutex g_link_mutex;
std::map<std::uint16_t, std::shared_ptr<DevelopmentTransportLink>> g_links;

std::shared_ptr<DevelopmentTransportLink> link_for_port(const std::uint16_t port) {
    std::lock_guard lock(g_link_mutex);
    auto& slot = g_links[port];
    if (!slot) {
        slot = std::make_shared<DevelopmentTransportLink>();
    }
    return slot;
}

void reset_link_for_port(const std::uint16_t port) {
    std::lock_guard lock(g_link_mutex);
    g_links.erase(port);
}

}  // namespace

DevelopmentSecureTransport::~DevelopmentSecureTransport() {
    close();
}

bool DevelopmentVirtualInterface::open(const std::uint16_t mtu) {
    opened_ = mtu >= 1280 && mtu <= 1420;
    return opened_;
}

Packet DevelopmentVirtualInterface::read_packet() {
    if (!opened_) {
        return {};
    }

    Packet result = std::move(packet_);
    packet_.clear();
    return result;
}

bool DevelopmentVirtualInterface::write_packet(
    const std::span<const std::byte> packet) {
    if (!opened_ || packet.empty()) {
        return false;
    }

    // 开发适配器把写入数据放回读取端，便于无管理员权限地测试数据路径。
    packet_.assign(packet.begin(), packet.end());
    return true;
}

void DevelopmentVirtualInterface::close() noexcept {
    opened_ = false;
    packet_.clear();
}

void DevelopmentVirtualInterface::inject_inbound_packet(Packet packet) {
    packet_ = std::move(packet);
}

bool DevelopmentSecureTransport::connect(const TunnelConfig& config) {
    config_ = config;
    link_ = link_for_port(config.port).get();
    connected_ = !config.endpoint.empty() && config.port != 0 && link_ != nullptr;
    data_channel_open_ = connected_;
    return connected_;
}

bool DevelopmentSecureTransport::perform_handshake() {
    return connected_;
}

bool DevelopmentSecureTransport::open_data_channel() {
    return connected_;
}

bool DevelopmentSecureTransport::send_payload(const std::span<const std::byte> payload) {
    if (!connected_ || link_ == nullptr || payload.empty()) {
        return false;
    }

    auto* link = static_cast<DevelopmentTransportLink*>(link_);
    Packet copy(payload.begin(), payload.end());
    std::lock_guard lock(link->mutex);
    if (config_.role == Role::client) {
        link->to_server.push_back(std::move(copy));
    } else {
        link->to_client.push_back(std::move(copy));
    }
    return true;
}

std::optional<Packet> DevelopmentSecureTransport::try_receive_payload() {
    if (!connected_ || link_ == nullptr) {
        return std::nullopt;
    }

    auto* link = static_cast<DevelopmentTransportLink*>(link_);
    std::lock_guard lock(link->mutex);
    std::deque<Packet>* queue =
        config_.role == Role::client ? &link->to_client : &link->to_server;
    if (queue->empty()) {
        return Packet{};
    }

    Packet frame = std::move(queue->front());
    queue->pop_front();
    return frame;
}

void DevelopmentSecureTransport::close() noexcept {
    if (connected_) {
        reset_link_for_port(config_.port);
    }
    connected_ = false;
    data_channel_open_ = false;
    link_ = nullptr;
}

}  // namespace tunnel
