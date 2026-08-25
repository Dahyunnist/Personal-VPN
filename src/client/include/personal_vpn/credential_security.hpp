#ifndef PERSONAL_VPN_CREDENTIAL_SECURITY_HPP
#define PERSONAL_VPN_CREDENTIAL_SECURITY_HPP

#include <filesystem>

namespace personal_vpn::client
{

void validate_private_key_permissions(const std::filesystem::path& private_key_file);

} // namespace personal_vpn::client

#endif
