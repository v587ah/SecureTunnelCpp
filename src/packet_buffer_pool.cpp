#include "tunnel/packet_buffer_pool.hpp"

#include <algorithm>
#include <utility>

namespace tunnel {

PacketBufferPool::PacketBufferPool(
    const std::size_t pool_capacity,
    const std::size_t default_capacity)
    : pool_capacity_(pool_capacity > 0 ? pool_capacity : 16),
      default_capacity_(default_capacity > 0 ? default_capacity : 2048) {
    free_list_.reserve(pool_capacity_);
}

std::vector<std::byte> PacketBufferPool::acquire(const std::size_t minimum_capacity) {
    const std::size_t target =
        std::max(minimum_capacity, default_capacity_);

    if (!free_list_.empty()) {
        std::vector<std::byte> buffer = std::move(free_list_.back());
        free_list_.pop_back();
        if (buffer.capacity() < target) {
            buffer.reserve(target);
        }
        buffer.clear();
        return buffer;
    }

    std::vector<std::byte> buffer;
    buffer.reserve(target);
    return buffer;
}

void PacketBufferPool::release(std::vector<std::byte>&& buffer) noexcept {
    if (free_list_.size() >= pool_capacity_) {
        return;
    }

    buffer.clear();
    free_list_.push_back(std::move(buffer));
}

std::size_t PacketBufferPool::available() const noexcept {
    return free_list_.size();
}

std::size_t PacketBufferPool::capacity() const noexcept {
    return pool_capacity_;
}

}  // namespace tunnel
