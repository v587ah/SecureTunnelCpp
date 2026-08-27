#include "tunnel/tunnel_engine.hpp"

#include "tunnel/inner_session.hpp"

#ifdef TUNNEL_HAS_MSQUIC
#include "tunnel/quic_secure_transport.hpp"
#endif

#include <chrono>
#include <thread>
#include <utility>

namespace tunnel {

TunnelEngine::TunnelEngine(
    TunnelConfig config,
    std::unique_ptr<VirtualInterface> virtual_interface,
    std::unique_ptr<SecureTransport> transport,
    std::unique_ptr<InnerSession> inner_session)
    : config_(std::move(config)),
      virtual_interface_(std::move(virtual_interface)),
      transport_(std::move(transport)),
      inner_session_(std::move(inner_session)),
      buffer_pool_(
          config_.data_plane.buffer_pool_capacity,
          config_.data_plane.buffer_default_capacity) {}

bool TunnelEngine::start() {
    std::string error;
    if (!config_.is_valid(error) || !virtual_interface_ || !transport_) {
        return false;
    }

    if (!inner_session_) {
        inner_session_ = create_inner_session(config_.inner);
        if (!inner_session_) {
            return false;
        }
    }

    relay_stats_.reset();
    scratch_sealed_ = buffer_pool_.acquire();
    scratch_opened_ = buffer_pool_.acquire();

    if (!session_.transition_to(SessionState::connecting)) {
        return false;
    }

    if (!virtual_interface_->open(config_.mtu) || !transport_->connect(config_)) {
        session_.transition_to(SessionState::failed);
        return false;
    }

    if (!session_.transition_to(SessionState::handshaking)) {
        return false;
    }

    // 服务端：Listener 就绪即可返回，在 relay 循环里等待客户端连入。
    if (config_.role == Role::server) {
        if (!transport_->perform_handshake()) {
            session_.transition_to(SessionState::failed);
            return false;
        }
        if (!inner_session_->is_ready()) {
            session_.transition_to(SessionState::failed);
            return false;
        }
        data_plane_stop_requested_ = false;
        return true;
    }

    if (!transport_->perform_handshake() || !transport_->open_data_channel()) {
        session_.transition_to(SessionState::failed);
        return false;
    }

    if (!inner_session_->is_ready()) {
        session_.transition_to(SessionState::failed);
        return false;
    }

    std::string route_error;
    if (!virtual_interface_->apply_client_routing(config_, route_error)) {
        session_.transition_to(SessionState::failed);
        return false;
    }

    data_plane_stop_requested_ = false;
    return session_.transition_to(SessionState::established);
}

bool TunnelEngine::ensure_session_ready() {
    if (session_.can_send_data()) {
        return true;
    }

    if (config_.role != Role::server ||
        session_.state() != SessionState::handshaking ||
        !transport_ || !inner_session_) {
        return false;
    }

    if (!transport_->open_data_channel()) {
        return false;
    }

    std::string route_error;
    if (!virtual_interface_->apply_client_routing(config_, route_error)) {
        session_.transition_to(SessionState::failed);
        return false;
    }

    return session_.transition_to(SessionState::established);
}

bool TunnelEngine::forward_outbound() {
    if (!session_.can_send_data() || !virtual_interface_ || !transport_ || !inner_session_) {
        return false;
    }

    Packet packet = virtual_interface_->read_packet();
    if (packet.empty()) {
        return true;
    }

    scratch_sealed_.clear();
    if (!inner_session_->seal(packet, scratch_sealed_)) {
        ++relay_stats_.seal_errors;
        return false;
    }

    if (!transport_->send_payload(scratch_sealed_)) {
        ++relay_stats_.transport_errors;
        return false;
    }

    ++relay_stats_.outbound_packets;
    relay_stats_.outbound_bytes += packet.size();
    return true;
}

std::size_t TunnelEngine::forward_inbound_batch(const std::size_t max_packets) {
    if (!session_.can_send_data() || !virtual_interface_ || !transport_ || !inner_session_) {
        return 0;
    }

    std::size_t processed = 0;
    for (std::size_t i = 0; i < max_packets; ++i) {
        std::optional<Packet> received = transport_->try_receive_payload();
        if (!received.has_value()) {
            break;
        }
        if (received->empty()) {
            break;
        }

        scratch_opened_.clear();
        if (!inner_session_->open(*received, scratch_opened_) || scratch_opened_.empty()) {
            ++relay_stats_.open_errors;
            return processed;
        }

        if (!virtual_interface_->write_packet(scratch_opened_)) {
            ++relay_stats_.transport_errors;
            return processed;
        }

        ++relay_stats_.inbound_packets;
        relay_stats_.inbound_bytes += scratch_opened_.size();
        ++processed;
    }

    return processed;
}

bool TunnelEngine::relay_once() {
    return relay_batch(1);
}

bool TunnelEngine::relay_batch(const std::size_t batch_size) {
    if (!session_.can_send_data()) {
        if (config_.role == Role::server &&
            session_.state() == SessionState::handshaking) {
            (void)ensure_session_ready();
            if (!session_.can_send_data()) {
                return true;
            }
        } else {
            return false;
        }
    }

    ++relay_stats_.relay_iterations;

    if (!forward_outbound()) {
        return false;
    }

    forward_inbound_batch(batch_size > 0 ? batch_size : 1);
    return true;
}

void TunnelEngine::run_data_plane_for(const std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    const std::size_t batch_size =
        config_.data_plane.batch_size > 0 ? config_.data_plane.batch_size : 1;
    const auto idle_sleep =
        std::chrono::milliseconds(config_.data_plane.idle_sleep_ms);

    while (!data_plane_stop_requested_ &&
           std::chrono::steady_clock::now() < deadline) {
        if (!session_.can_send_data()) {
            if (config_.role == Role::server &&
                session_.state() == SessionState::handshaking) {
                if (!ensure_session_ready()) {
                    std::this_thread::sleep_for(idle_sleep);
                    continue;
                }
            }
            if (!session_.can_send_data()) {
                if (config_.role == Role::server && !data_plane_stop_requested_) {
                    reset_server_session();
                    std::this_thread::sleep_for(idle_sleep);
                    continue;
                }
                break;
            }
        }

        const auto inbound_before = relay_stats_.inbound_packets;
        const auto outbound_before = relay_stats_.outbound_packets;

        if (!relay_batch(batch_size)) {
            if (data_plane_stop_requested_) {
                break;
            }
            if (config_.role == Role::server &&
                session_.state() == SessionState::handshaking) {
                std::this_thread::sleep_for(idle_sleep);
                continue;
            }
            if (config_.role == Role::server) {
                reset_server_session();
                std::this_thread::sleep_for(idle_sleep);
                continue;
            }
            session_.transition_to(SessionState::failed);
            break;
        }

        if (!session_.can_send_data()) {
            continue;
        }

        if (relay_stats_.inbound_packets == inbound_before &&
            relay_stats_.outbound_packets == outbound_before) {
            std::this_thread::sleep_for(idle_sleep);
        }
    }
}

void TunnelEngine::request_data_plane_stop() noexcept {
    data_plane_stop_requested_ = true;
}

void TunnelEngine::reset_server_session() noexcept {
    if (config_.role != Role::server) {
        return;
    }

#ifdef TUNNEL_HAS_MSQUIC
    if (transport_ != nullptr) {
        if (auto* quic = dynamic_cast<QuicSecureTransport*>(transport_.get())) {
            quic->reset_for_next_client();
        }
    }
#endif

    (void)session_.reset_to_handshaking();
}

void TunnelEngine::stop() noexcept {
    data_plane_stop_requested_ = true;

    if (session_.state() == SessionState::established) {
        session_.transition_to(SessionState::closing);
    }

    if (virtual_interface_) {
        virtual_interface_->clear_client_routing();
    }
    if (transport_) {
        transport_->close();
    }
    if (virtual_interface_) {
        virtual_interface_->close();
    }

    buffer_pool_.release(std::move(scratch_sealed_));
    buffer_pool_.release(std::move(scratch_opened_));

    if (session_.state() == SessionState::closing ||
        session_.state() == SessionState::failed) {
        session_.transition_to(SessionState::stopped);
    }
}

const Session& TunnelEngine::session() const noexcept {
    return session_;
}

const RelayStats& TunnelEngine::relay_stats() const noexcept {
    return relay_stats_;
}

}  // namespace tunnel
