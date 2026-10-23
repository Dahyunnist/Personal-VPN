#ifndef PERSONAL_VPN_BROKER_SERVICE_POLICY_HPP
#define PERSONAL_VPN_BROKER_SERVICE_POLICY_HPP

#ifdef _WIN32
#include <filesystem>
#endif
#include <string_view>

#include "personal_vpn/client_config.hpp"

namespace personal_vpn::broker
{

[[nodiscard]] client::ClientNetworkPolicy parse_broker_network_policy(
    std::string_view json);

#ifdef _WIN32
[[nodiscard]] client::ClientNetworkPolicy load_broker_network_policy(
    const std::filesystem::path& policy_file);
#endif

} // namespace personal_vpn::broker

#endif
