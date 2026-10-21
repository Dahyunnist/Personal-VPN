#ifndef PERSONAL_VPN_SERVER_CONFIG_HPP
#define PERSONAL_VPN_SERVER_CONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace personal_vpn::server
{

enum class MetricsFormat
{
    Json,
    Prometheus
};

struct ServerConfig
{
    std::string listen_address{"0.0.0.0"};
    std::uint16_t listen_port{8'443U};
    std::string tun_name{"pvpn0"};
    std::string certificate_chain_file;
    std::string private_key_file;
    std::string client_ca_file;
    std::string client_crl_file;
    std::string first_lease_address{"10.8.0.2"};
    std::string last_lease_address{"10.8.0.254"};
    std::string gateway_address{"10.8.0.1"};
    std::string ipv6_prefix_address{"fd00:8::"};
    std::string ipv6_gateway_address{"fd00:8::1"};
    std::uint8_t prefix_length{24U};
    std::uint16_t mtu{1'400U};
    std::uint32_t lease_seconds{3'600U};
    std::size_t worker_threads{2U};
    std::size_t maximum_sessions{1'024U};
    std::uint32_t handshake_timeout_seconds{10U};
    std::uint32_t idle_timeout_seconds{300U};
    std::uint32_t metrics_interval_seconds{60U};
    MetricsFormat metrics_format{MetricsFormat::Json};
    bool show_help{false};
    bool show_version{false};
};

[[nodiscard]] ServerConfig parse_server_arguments(const std::vector<std::string>& arguments);
[[nodiscard]] std::string server_usage(const std::string& executable_name);

} // namespace personal_vpn::server

#endif
