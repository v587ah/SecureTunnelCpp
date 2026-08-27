#include "tunnel/relay_stats.hpp"

namespace tunnel {

void RelayStats::reset() noexcept {
    relay_iterations = 0;
    outbound_packets = 0;
    outbound_bytes = 0;
    inbound_packets = 0;
    inbound_bytes = 0;
    seal_errors = 0;
    open_errors = 0;
    transport_errors = 0;
}

}  // namespace tunnel
