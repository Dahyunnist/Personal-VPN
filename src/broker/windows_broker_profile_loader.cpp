#include "personal_vpn/windows_broker_profile_loader.hpp"

#ifdef _WIN32

#include "personal_vpn/windows_broker_profile.hpp"

namespace personal_vpn::broker
{

client::ClientConfig load_impersonated_profile(
    const std::string& profile_id,
    const client::ClientNetworkPolicy& network_policy)
{
    return client::load_client_config(resolve_impersonated_profile_path(profile_id),
                                      network_policy);
}

} // namespace personal_vpn::broker

#endif
