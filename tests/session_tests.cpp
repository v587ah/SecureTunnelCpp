#include "tunnel/config.hpp"
#include "tunnel/session.hpp"

#include <cassert>
#include <string>

int main() {
    tunnel::Session session;

    // 未握手时不能发送业务数据。
    assert(!session.can_send_data());
    assert(!session.transition_to(tunnel::SessionState::established));

    assert(session.transition_to(tunnel::SessionState::connecting));
    assert(session.transition_to(tunnel::SessionState::handshaking));
    assert(session.transition_to(tunnel::SessionState::established));
    assert(session.can_send_data());
    assert(session.epoch() == 1);

    assert(session.transition_to(tunnel::SessionState::closing));
    assert(session.transition_to(tunnel::SessionState::stopped));

    tunnel::TunnelConfig config;
    std::string error;
    assert(config.is_valid(error));

    config.allow_zero_rtt = true;
    assert(!config.is_valid(error));
    return 0;
}
