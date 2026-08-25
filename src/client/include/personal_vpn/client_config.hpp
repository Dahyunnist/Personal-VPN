#ifndef PERSONAL_VPN_CLIENT_CONFIG_HPP
#define PERSONAL_VPN_CLIENT_CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace personal_vpn::client
{

struct ClientConfig
{
    std::string server_host;
    std::uint16_t server_port{8'443U};
    std::string expected_server_name;
    std::filesystem::path ca_file;
    std::filesystem::path certificate_chain_file;
    std::filesystem::path private_key_file;
    std::uint16_t requested_mtu{1'400U};
    std::vector<std::string> routes;
    bool allow_default_route{false};
};

[[nodiscard]] ClientConfig load_client_config(const std::filesystem::path& config_file);

} // namespace personal_vpn::client

#endif
