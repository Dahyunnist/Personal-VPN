#ifndef PERSONAL_VPN_WINDOWS_BROKER_PROFILE_LOADER_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_PROFILE_LOADER_HPP

#ifdef _WIN32

#include <string>

#include "personal_vpn/client_config.hpp"

namespace personal_vpn::broker
{

// Reads the profile and credentials while the current thread impersonates its owner.
[[nodiscard]] client::ClientConfig load_impersonated_profile(
    const std::string& profile_id,
    const client::ClientNetworkPolicy& network_policy);

} // namespace personal_vpn::broker

#endif
#endif
