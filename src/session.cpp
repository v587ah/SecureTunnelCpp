#include "tunnel/session.hpp"

namespace tunnel {

SessionState Session::state() const noexcept {
    return state_;
}

std::uint64_t Session::epoch() const noexcept {
    return epoch_;
}

bool Session::can_send_data() const noexcept {
    return state_ == SessionState::established;
}

bool Session::transition_to(const SessionState next) noexcept {
    const bool allowed =
        (state_ == SessionState::stopped && next == SessionState::connecting) ||
        (state_ == SessionState::connecting && next == SessionState::handshaking) ||
        (state_ == SessionState::handshaking && next == SessionState::established) ||
        (state_ == SessionState::established && next == SessionState::closing) ||
        (state_ == SessionState::closing && next == SessionState::stopped) ||
        (state_ != SessionState::stopped && next == SessionState::failed) ||
        (state_ == SessionState::failed && next == SessionState::stopped);

    if (!allowed) {
        return false;
    }

    state_ = next;
    if (next == SessionState::established) {
        ++epoch_;
    }
    return true;
}

bool Session::reset_to_handshaking() noexcept {
    if (state_ == SessionState::established) {
        (void)transition_to(SessionState::closing);
    }
    if (state_ == SessionState::closing || state_ == SessionState::failed) {
        (void)transition_to(SessionState::stopped);
    }
    if (state_ == SessionState::stopped) {
        (void)transition_to(SessionState::connecting);
    }
    if (state_ == SessionState::connecting) {
        return transition_to(SessionState::handshaking);
    }
    return state_ == SessionState::handshaking;
}

std::string_view to_string(const SessionState state) noexcept {
    switch (state) {
        case SessionState::stopped:
            return "已停止";
        case SessionState::connecting:
            return "正在连接";
        case SessionState::handshaking:
            return "正在安全握手";
        case SessionState::established:
            return "连接已建立";
        case SessionState::closing:
            return "正在关闭";
        case SessionState::failed:
            return "连接失败";
    }
    return "未知状态";
}

}  // namespace tunnel
