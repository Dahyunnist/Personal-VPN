#ifndef PERSONAL_VPN_CREDENTIAL_SECURITY_HPP
#define PERSONAL_VPN_CREDENTIAL_SECURITY_HPP

#include <filesystem>
#include <cstdio>
#include <memory>

namespace personal_vpn::client
{

using ValidatedPrivateKeyFile = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

[[nodiscard]] ValidatedPrivateKeyFile open_validated_private_key_file(
    const std::filesystem::path& private_key_file);
void validate_private_key_permissions(const std::filesystem::path& private_key_file);

} // namespace personal_vpn::client

#endif
