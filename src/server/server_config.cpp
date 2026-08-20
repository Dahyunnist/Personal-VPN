#include "personal_vpn/server_config.hpp"

#include "personal_vpn/lease_manager.hpp"

#include <boost/asio/ip/address.hpp>

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace personal_vpn::server
{
namespace
{

std::uint64_t parse_unsigned(const std::string& option,
                             const std::string& value,
                             const std::uint64_t minimum,
                             const std::uint64_t maximum)
{
    std::uint64_t parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (value.empty() || result.ec != std::errc{} ||
        result.ptr != value.data() + value.size() || parsed < minimum || parsed > maximum)
    {
        throw std::invalid_argument("invalid value for " + option + ": " + value);
    }
    return parsed;
}

const std::string& require_value(const std::vector<std::string>& arguments,
                                 std::size_t& index)
{
    if (index + 1U >= arguments.size())
    {
        throw std::invalid_argument("missing value for " + arguments[index]);
    }
    ++index;
    return arguments[index];
}

void validate(const ServerConfig& config)
{
    if (config.show_help)
    {
        return;
    }
    if (config.certificate_chain_file.empty() || config.private_key_file.empty() ||
        config.client_ca_file.empty())
    {
        throw std::invalid_argument(
            "--server-cert, --server-key, and --client-ca are required");
    }
    boost::system::error_code address_error;
    const auto listen_address = boost::asio::ip::make_address(config.listen_address, address_error);
    if (address_error || !listen_address.is_v4())
    {
        throw std::invalid_argument("--listen-address must be a valid IPv4 address");
    }
    if (config.tun_name.empty() || config.tun_name.size() > 15U)
    {
        throw std::invalid_argument("--tun-name must contain between 1 and 15 bytes");
    }
    core::LeaseManager policy(core::parse_ipv4_address(config.first_lease_address),
                              core::parse_ipv4_address(config.last_lease_address),
                              core::parse_ipv4_address(config.gateway_address),
                              config.prefix_length,
                              config.mtu,
                              std::chrono::seconds(config.lease_seconds));
    static_cast<void>(policy);
}

} // namespace

ServerConfig parse_server_arguments(const std::vector<std::string>& arguments)
{
    ServerConfig config;
    for (std::size_t index = 0U; index < arguments.size(); ++index)
    {
        const auto& option = arguments[index];
        if (option == "--help" || option == "-h")
        {
            config.show_help = true;
            continue;
        }
        const auto& value = require_value(arguments, index);
        if (option == "--listen-address")
        {
            config.listen_address = value;
        }
        else if (option == "--port")
        {
            config.listen_port = static_cast<std::uint16_t>(
                parse_unsigned(option, value, 1U, std::numeric_limits<std::uint16_t>::max()));
        }
        else if (option == "--tun-name")
        {
            config.tun_name = value;
        }
        else if (option == "--server-cert")
        {
            config.certificate_chain_file = value;
        }
        else if (option == "--server-key")
        {
            config.private_key_file = value;
        }
        else if (option == "--client-ca")
        {
            config.client_ca_file = value;
        }
        else if (option == "--client-crl")
        {
            config.client_crl_file = value;
        }
        else if (option == "--lease-start")
        {
            config.first_lease_address = value;
        }
        else if (option == "--lease-end")
        {
            config.last_lease_address = value;
        }
        else if (option == "--gateway")
        {
            config.gateway_address = value;
        }
        else if (option == "--prefix-length")
        {
            config.prefix_length =
                static_cast<std::uint8_t>(parse_unsigned(option, value, 1U, 32U));
        }
        else if (option == "--mtu")
        {
            config.mtu = static_cast<std::uint16_t>(parse_unsigned(
                option, value, protocol::kMinimumIpv4Mtu, protocol::kMaximumTunnelMtu));
        }
        else if (option == "--lease-seconds")
        {
            config.lease_seconds = static_cast<std::uint32_t>(parse_unsigned(
                option, value, 1U, std::numeric_limits<std::uint32_t>::max()));
        }
        else if (option == "--threads")
        {
            config.worker_threads =
                static_cast<std::size_t>(parse_unsigned(option, value, 1U, 256U));
        }
        else if (option == "--max-sessions")
        {
            config.maximum_sessions =
                static_cast<std::size_t>(parse_unsigned(option, value, 1U, 1'000'000U));
        }
        else if (option == "--handshake-timeout")
        {
            config.handshake_timeout_seconds =
                static_cast<std::uint32_t>(parse_unsigned(option, value, 1U, 300U));
        }
        else if (option == "--idle-timeout")
        {
            config.idle_timeout_seconds =
                static_cast<std::uint32_t>(parse_unsigned(option, value, 1U, 86'400U));
        }
        else if (option == "--metrics-interval")
        {
            config.metrics_interval_seconds =
                static_cast<std::uint32_t>(parse_unsigned(option, value, 0U, 86'400U));
        }
        else
        {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    validate(config);
    return config;
}

std::string server_usage(const std::string& executable_name)
{
    return "Usage: " + executable_name + " [options]\n\n"
           "Required:\n"
           "  --server-cert PATH       Server certificate chain (PEM)\n"
           "  --server-key PATH        Server private key (PEM)\n"
           "  --client-ca PATH         CA bundle trusted for client certificates\n\n"
           "Optional security:\n"
           "  --client-crl PATH        PEM CRL for revoked client certificates\n\n"
           "Network:\n"
           "  --listen-address IPV4    Listen address (default: 0.0.0.0)\n"
           "  --port NUMBER            Listen port (default: 8443)\n"
           "  --tun-name NAME          Existing/created TUN name (default: pvpn0)\n"
           "  --lease-start IPV4       First client address (default: 10.8.0.2)\n"
           "  --lease-end IPV4         Last client address (default: 10.8.0.254)\n"
           "  --gateway IPV4           Tunnel gateway (default: 10.8.0.1)\n"
           "  --prefix-length NUMBER   Tunnel prefix length (default: 24)\n"
           "  --mtu NUMBER             Tunnel MTU (default: 1400)\n"
           "  --lease-seconds NUMBER   Lease lifetime (default: 3600)\n\n"
           "Runtime:\n"
           "  --threads NUMBER         I/O workers, 1-256 (default: 2)\n"
           "  --max-sessions NUMBER    Admission limit (default: 1024)\n"
           "  --handshake-timeout SEC  TLS handshake deadline, 1-300 (default: 10)\n"
           "  --idle-timeout SEC       Inbound frame idle deadline (default: 300)\n"
           "  --metrics-interval SEC   JSON metrics interval; 0 disables (default: 60)\n"
           "  -h, --help               Show this help\n";
}

} // namespace personal_vpn::server
