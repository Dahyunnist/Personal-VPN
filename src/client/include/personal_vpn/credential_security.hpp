#ifndef PERSONAL_VPN_CREDENTIAL_SECURITY_HPP
#define PERSONAL_VPN_CREDENTIAL_SECURITY_HPP

#include <filesystem>
#include <cstdio>
#include <memory>

namespace personal_vpn::client
{

struct ValidatedPrivateKeyFileDeleter
{
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr)
        {
            static_cast<void>(std::fclose(file));
        }
    }
};

using ValidatedPrivateKeyFile = std::unique_ptr<std::FILE, ValidatedPrivateKeyFileDeleter>;

[[nodiscard]] ValidatedPrivateKeyFile open_validated_private_key_file(
    const std::filesystem::path& private_key_file);
void validate_private_key_permissions(const std::filesystem::path& private_key_file);

} // namespace personal_vpn::client

#endif
