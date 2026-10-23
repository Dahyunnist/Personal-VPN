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

// Removes only the current user's imported profile files. Repeated calls are safe.
void delete_broker_profile(const std::string& profile_id);

} // namespace personal_vpn::broker

#endif
#endif
