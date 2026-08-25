#ifndef PERSONAL_VPN_SERVER_METRICS_HPP
#define PERSONAL_VPN_SERVER_METRICS_HPP

#include <atomic>
#include <cstdint>

namespace personal_vpn::server
{

struct ServerMetricsSnapshot
{
    std::uint64_t accepted_connections{0U};
    std::uint64_t capacity_rejections{0U};
    std::uint64_t handshake_failures{0U};
    std::uint64_t handshake_timeouts{0U};
    std::uint64_t idle_timeouts{0U};
    std::uint64_t established_sessions{0U};
    std::uint64_t closed_sessions{0U};
    std::uint64_t queue_overflows{0U};
    std::uint64_t uplink_packets{0U};
    std::uint64_t uplink_bytes{0U};
    std::uint64_t downlink_packets{0U};
    std::uint64_t downlink_bytes{0U};
};

class ServerMetrics final
{
   public:
    [[nodiscard]] ServerMetricsSnapshot snapshot() const noexcept;

    std::atomic<std::uint64_t> accepted_connections{0U};
    std::atomic<std::uint64_t> capacity_rejections{0U};
    std::atomic<std::uint64_t> handshake_failures{0U};
    std::atomic<std::uint64_t> handshake_timeouts{0U};
    std::atomic<std::uint64_t> idle_timeouts{0U};
    std::atomic<std::uint64_t> established_sessions{0U};
    std::atomic<std::uint64_t> closed_sessions{0U};
    std::atomic<std::uint64_t> queue_overflows{0U};
    std::atomic<std::uint64_t> uplink_packets{0U};
    std::atomic<std::uint64_t> uplink_bytes{0U};
    std::atomic<std::uint64_t> downlink_packets{0U};
    std::atomic<std::uint64_t> downlink_bytes{0U};
};

inline ServerMetricsSnapshot ServerMetrics::snapshot() const noexcept
{
    return ServerMetricsSnapshot{accepted_connections.load(),
                                 capacity_rejections.load(),
                                 handshake_failures.load(),
                                 handshake_timeouts.load(),
                                 idle_timeouts.load(),
                                 established_sessions.load(),
                                 closed_sessions.load(),
                                 queue_overflows.load(),
                                 uplink_packets.load(),
                                 uplink_bytes.load(),
                                 downlink_packets.load(),
                                 downlink_bytes.load()};
}

} // namespace personal_vpn::server

#endif
