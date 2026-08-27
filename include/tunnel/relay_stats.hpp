#pragma once

#include <cstdint>

namespace tunnel {

// 数据面 relay 统计，便于性能分析与回归对比。
struct RelayStats {
    std::uint64_t relay_iterations{0};
    std::uint64_t outbound_packets{0};
    std::uint64_t outbound_bytes{0};
    std::uint64_t inbound_packets{0};
    std::uint64_t inbound_bytes{0};
    std::uint64_t seal_errors{0};
    std::uint64_t open_errors{0};
    std::uint64_t transport_errors{0};

    void reset() noexcept;
};

}  // namespace tunnel
