#include "tunnel/inner_session.hpp"

#include <iostream>

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
    tunnel::InnerSessionConfig dev_config;
    auto session = tunnel::create_inner_session(dev_config);
    if (!expect(session != nullptr, "无法创建开发内层会话")) {
        return 1;
    }

    const std::vector<std::byte> plaintext{
        std::byte{0x45},
        std::byte{0x00},
        std::byte{0xAB},
    };

    std::vector<std::byte> sealed;
    if (!expect(session->seal(plaintext, sealed), "seal 失败")) {
        return 1;
    }

    std::vector<std::byte> opened;
    if (!expect(session->open(sealed, opened), "open 失败")) {
        return 1;
    }
    if (!expect(opened == plaintext, "解密后载荷不一致")) {
        return 1;
    }

    if (!expect(!session->open(sealed, opened), "重放帧应被拒绝")) {
        return 1;
    }

    std::cout << "inner_session_tests passed\n";
    return 0;
}
