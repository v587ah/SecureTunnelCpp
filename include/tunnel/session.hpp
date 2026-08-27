#pragma once

#include <cstdint>
#include <string_view>

namespace tunnel {

// 会话状态机用于约束连接生命周期，防止未完成握手就发送数据。
enum class SessionState {
    stopped,
    connecting,
    handshaking,
    established,
    closing,
    failed,
};

class Session {
public:
    [[nodiscard]] SessionState state() const noexcept;
    [[nodiscard]] std::uint64_t epoch() const noexcept;
    [[nodiscard]] bool can_send_data() const noexcept;

    // 只有合法的状态迁移才返回 true。
    // epoch 在每次成功建连时递增，可用于区分旧连接的迟到数据包。
    bool transition_to(SessionState next) noexcept;

    // 服务端：客户端断开后回到 handshaking，继续等待下一连接。
    bool reset_to_handshaking() noexcept;

private:
    SessionState state_{SessionState::stopped};
    std::uint64_t epoch_{0};
};

[[nodiscard]] std::string_view to_string(SessionState state) noexcept;

}  // namespace tunnel
