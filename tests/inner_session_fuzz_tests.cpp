#include "tunnel/inner_session.hpp"

#include <iostream>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

std::vector<std::byte> make_dev_frame(
    std::uint64_t sequence,
    const std::vector<std::byte>& payload) {
    std::vector<std::byte> frame;
    frame.push_back(static_cast<std::byte>(tunnel::kInnerFrameVersionDev));
    for (int shift = 56; shift >= 0; shift -= 8) {
        frame.push_back(static_cast<std::byte>((sequence >> shift) & 0xFF));
    }
    frame.push_back(static_cast<std::byte>((payload.size() >> 8) & 0xFF));
    frame.push_back(static_cast<std::byte>(payload.size() & 0xFF));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

}  // namespace

int main() {
    tunnel::InnerSessionConfig config;
    auto session = tunnel::create_inner_session(config);
    if (!expect(session != nullptr, "无法创建内层会话")) {
        return 1;
    }

    std::vector<std::byte> opened;

    // 空帧
    if (!expect(!session->open({}, opened), "空帧应被拒绝")) {
        return 1;
    }

    // 版本错误
    {
        std::vector<std::byte> bad{std::byte{0xFF}, std::byte{0}, std::byte{0},
                                   std::byte{0}, std::byte{0}, std::byte{0},
                                   std::byte{0}, std::byte{0}, std::byte{0},
                                   std::byte{0}, std::byte{0}};
        if (!expect(!session->open(bad, opened), "错误版本应被拒绝")) {
            return 1;
        }
    }

    // 长度字段超出实际数据
    {
        std::vector<std::byte> bad = make_dev_frame(1, {std::byte{0x01}});
        bad[10] = std::byte{0xFF};
        bad[11] = std::byte{0xFF};
        if (!expect(!session->open(bad, opened), "非法长度应被拒绝")) {
            return 1;
        }
    }

    // 合法帧后重放
    {
        const auto frame = make_dev_frame(10, {std::byte{0xAB}, std::byte{0xCD}});
        if (!expect(session->open(frame, opened), "合法帧应通过")) {
            return 1;
        }
        if (!expect(!session->open(frame, opened), "重放帧应被拒绝")) {
            return 1;
        }
    }

    // 乱序（序号倒退）
    {
        const auto frame = make_dev_frame(5, {std::byte{0x01}});
        if (!expect(!session->open(frame, opened), "乱序帧应被拒绝")) {
            return 1;
        }
    }

    // 超大载荷声明
    {
        std::vector<std::byte> bad;
        bad.push_back(static_cast<std::byte>(tunnel::kInnerFrameVersionDev));
        for (int i = 0; i < 8; ++i) {
            bad.push_back(std::byte{0});
        }
        bad.push_back(std::byte{0x08});
        bad.push_back(std::byte{0x00});
        if (!expect(!session->open(bad, opened), "超大载荷声明应被拒绝")) {
            return 1;
        }
    }

    std::cout << "inner_session_fuzz_tests passed\n";
    return 0;
}
