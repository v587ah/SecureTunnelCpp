#include "tunnel/packet_buffer_pool.hpp"

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
    tunnel::PacketBufferPool pool(4, 128);

    auto buffer_a = pool.acquire(64);
    buffer_a.push_back(std::byte{0x01});
    if (!expect(pool.available() == 0, "借出后可用数应为 0")) {
        return 1;
    }

    pool.release(std::move(buffer_a));
    if (!expect(pool.available() == 1, "归还后可用数应为 1")) {
        return 1;
    }

    auto buffer_b = pool.acquire();
    if (!expect(buffer_b.empty(), "复用缓冲应为空")) {
        return 1;
    }

    for (std::size_t i = 0; i < 8; ++i) {
        auto temp = pool.acquire();
        temp.push_back(std::byte{0xFF});
        pool.release(std::move(temp));
    }

    if (!expect(pool.available() <= pool.capacity(), "池容量不应被突破")) {
        return 1;
    }

    std::cout << "packet_buffer_pool_tests passed\n";
    return 0;
}
