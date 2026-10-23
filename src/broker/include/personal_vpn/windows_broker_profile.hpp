#ifndef PERSONAL_VPN_WINDOWS_BROKER_PROFILE_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_PROFILE_HPP

#ifdef _WIN32

#include <filesystem>
#include <string>

namespace personal_vpn::broker
{

// Requires an active thread impersonation token. Never accepts a caller-supplied path.
[[nodiscard]] std::filesystem::path resolve_impersonated_profile_path(
    const std::string& profile_id);

} // namespace personal_vpn::broker

#endif
#endif
