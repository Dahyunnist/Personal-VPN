#include "personal_vpn/metrics_exporter.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main()
{
    personal_vpn::server::ServerMetricsSnapshot metrics;
    metrics.accepted_connections = 17U;
    metrics.queue_overflows = 2U;
    metrics.uplink_bytes = 4'096U;
    metrics.downlink_bytes = 8'192U;
    const auto json = personal_vpn::server::render_server_metrics_json(metrics, 3U);
    check(json.find("\"schema_version\":1") != std::string::npos,
          "JSON metrics declare schema version");
    check(json.find("\"accepted\":17") != std::string::npos &&
              json.find("\"queue_overflows\":2") != std::string::npos &&
              json.find("\"uplink_bytes\":4096") != std::string::npos &&
              json.find("\"downlink_bytes\":8192") != std::string::npos &&
              json.find("\"active\":3") != std::string::npos,
          "JSON includes lifecycle, queue, active, and byte counters");
    check(json.find("fingerprint") == std::string::npos &&
              json.find("client_address") == std::string::npos,
          "JSON metrics omit identity and address labels");

    const auto prometheus =
        personal_vpn::server::render_server_metrics_prometheus(metrics, 3U);
    check(prometheus.find("personal_vpn_metrics_schema_info{version=\"1\"} 1") !=
              std::string::npos,
          "Prometheus exposition declares schema version");
    check(prometheus.find("# TYPE personal_vpn_accepted_connections counter\n"
                          "personal_vpn_accepted_connections_total 17") !=
              std::string::npos &&
              prometheus.find("personal_vpn_queue_overflows_total 2") != std::string::npos &&
              prometheus.find("personal_vpn_uplink_bytes_total 4096") != std::string::npos &&
              prometheus.find("personal_vpn_downlink_bytes_total 8192") != std::string::npos &&
              prometheus.find("personal_vpn_active_sessions 3") != std::string::npos,
          "Prometheus exposes bounded counters and active-session gauge");
    check(prometheus.find("client_address") == std::string::npos &&
              prometheus.find("fingerprint") == std::string::npos,
          "Prometheus exposition has no client identity labels");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
