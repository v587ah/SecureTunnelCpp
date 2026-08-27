#pragma once

#include <cstddef>
#include <vector>

namespace tunnel {

// 固定容量缓冲池，减少 relay 路径上的重复堆分配。
class PacketBufferPool {
public:
    PacketBufferPool() = default;
    PacketBufferPool(std::size_t pool_capacity, std::size_t default_capacity);

    [[nodiscard]] std::vector<std::byte> acquire(std::size_t minimum_capacity = 0);
    void release(std::vector<std::byte>&& buffer) noexcept;

    [[nodiscard]] std::size_t available() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    std::size_t pool_capacity_{16};
    std::size_t default_capacity_{2048};
    std::vector<std::vector<std::byte>> free_list_;
};

}  // namespace tunnel
