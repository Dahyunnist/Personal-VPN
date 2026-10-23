#ifndef PERSONAL_VPN_WINDOWS_BROKER_PROFILE_STORE_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_PROFILE_STORE_HPP

#ifdef _WIN32

#include <filesystem>
#include <string>

namespace personal_vpn::broker
{

struct StoredBrokerProfile
{
    std::string profile_id;
    std::filesystem::path config_path;
};

// Copies a validated profile and its credentials to the current user's fixed broker profile root.
[[nodiscard]] StoredBrokerProfile store_broker_profile(
    const std::filesystem::path& source_config);

} // namespace personal_vpn::broker

#endif
#endif
