#ifndef PERSONAL_VPN_METRICS_EXPORTER_HPP
#define PERSONAL_VPN_METRICS_EXPORTER_HPP

#include "personal_vpn/server_metrics.hpp"

#include <cstdint>
#include <string>

namespace personal_vpn::server
{

[[nodiscard]] std::string render_server_metrics_json(const ServerMetricsSnapshot& metrics,
                                                     std::uint64_t active_sessions);
[[nodiscard]] std::string render_server_metrics_prometheus(
    const ServerMetricsSnapshot& metrics,
    std::uint64_t active_sessions);

} // namespace personal_vpn::server

#endif
