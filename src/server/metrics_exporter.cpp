#include "personal_vpn/metrics_exporter.hpp"

#include <sstream>

namespace personal_vpn::server
{

std::string render_server_metrics_json(const ServerMetricsSnapshot& metrics,
                                       const std::uint64_t active_sessions)
{
    std::ostringstream output;
    output << "{\"event\":\"server_metrics\",\"schema_version\":1"
           << ",\"accepted\":" << metrics.accepted_connections
           << ",\"capacity_rejections\":" << metrics.capacity_rejections
           << ",\"handshake_failures\":" << metrics.handshake_failures
           << ",\"handshake_timeouts\":" << metrics.handshake_timeouts
           << ",\"idle_timeouts\":" << metrics.idle_timeouts
           << ",\"established\":" << metrics.established_sessions
           << ",\"closed\":" << metrics.closed_sessions
           << ",\"active\":" << active_sessions
           << ",\"queue_overflows\":" << metrics.queue_overflows
           << ",\"uplink_packets\":" << metrics.uplink_packets
           << ",\"uplink_bytes\":" << metrics.uplink_bytes
           << ",\"downlink_packets\":" << metrics.downlink_packets
           << ",\"downlink_bytes\":" << metrics.downlink_bytes << "}\n";
    return output.str();
}

std::string render_server_metrics_prometheus(const ServerMetricsSnapshot& metrics,
                                             const std::uint64_t active_sessions)
{
    std::ostringstream output;
    output << "# HELP personal_vpn_metrics_schema_info Metrics schema version.\n"
           << "# TYPE personal_vpn_metrics_schema_info gauge\n"
           << "personal_vpn_metrics_schema_info{version=\"1\"} 1\n";
    const auto counter = [&output](const char* name, const std::uint64_t value)
    {
        output << "# TYPE personal_vpn_" << name << " counter\n"
               << "personal_vpn_" << name << "_total " << value << '\n';
    };
    counter("accepted_connections", metrics.accepted_connections);
    counter("capacity_rejections", metrics.capacity_rejections);
    counter("handshake_failures", metrics.handshake_failures);
    counter("handshake_timeouts", metrics.handshake_timeouts);
    counter("idle_timeouts", metrics.idle_timeouts);
    counter("established_sessions", metrics.established_sessions);
    counter("closed_sessions", metrics.closed_sessions);
    counter("queue_overflows", metrics.queue_overflows);
    counter("uplink_packets", metrics.uplink_packets);
    counter("uplink_bytes", metrics.uplink_bytes);
    counter("downlink_packets", metrics.downlink_packets);
    counter("downlink_bytes", metrics.downlink_bytes);
    output << "# TYPE personal_vpn_active_sessions gauge\n"
           << "personal_vpn_active_sessions " << active_sessions << '\n';
    return output.str();
}

} // namespace personal_vpn::server
