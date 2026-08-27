#pragma once

#include "tunnel/tunnel_engine.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tunnel {

// 基于 MsQuic 的 QUIC + TLS 1.3 传输实现（Windows Schannel）。
// 负责：注册 MsQuic、加载证书、客户端建连/服务端监听、同步等待握手完成。
// 不负责：内层 WireGuard/Noise 加密、虚拟网卡数据转发（后续里程碑）。
class QuicSecureTransport final : public SecureTransport {
public:
    QuicSecureTransport() = default;
    ~QuicSecureTransport() override;

    QuicSecureTransport(const QuicSecureTransport&) = delete;
    QuicSecureTransport& operator=(const QuicSecureTransport&) = delete;

    bool connect(const TunnelConfig& config) override;
    bool perform_handshake() override;
    bool open_data_channel() override;
    bool send_payload(std::span<const std::byte> payload) override;
    std::optional<Packet> try_receive_payload() override;
    void close() noexcept override;

    // 服务端专用：关闭当前连接但保持 Listener，等待下一客户端。
    void reset_for_next_client() noexcept;

    [[nodiscard]] const std::string& last_error() const noexcept;

private:
    bool ensure_api_open();
    bool open_registration();
    bool load_configuration();
    bool start_client();
    bool start_server_listener();
    bool wait_for_handshake(std::chrono::milliseconds timeout);
    bool wait_for_stream_ready(std::chrono::milliseconds timeout);
    bool setup_client_stream();
    void set_error(std::string message);

    TunnelConfig config_;
    std::string last_error_;

    // MsQuic 句柄以 void* 保存，避免在公共头文件里暴露 msquic.h。
    const void* msquic_{nullptr};
    void* registration_{nullptr};
    void* configuration_{nullptr};
    void* connection_{nullptr};
    void* listener_{nullptr};

    void* handshake_state_{nullptr};
    void* server_context_{nullptr};
    void* stream_channel_{nullptr};
    void* connection_context_{nullptr};
    void* stored_credentials_{nullptr};

    bool api_open_{false};
    bool listener_started_{false};
    bool data_channel_open_{false};
    bool closed_{false};
};

}  // namespace tunnel