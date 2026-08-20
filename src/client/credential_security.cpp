#include "personal_vpn/credential_security.hpp"

#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Aclapi.h>

#include <array>
#include <memory>
#include <system_error>
#endif

namespace personal_vpn::client
{

void validate_private_key_permissions(const std::filesystem::path& private_key_file)
{
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW(private_key_file.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "inspect client private key");
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
    {
        throw std::invalid_argument("client private key must not be a reparse point");
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    const auto result = GetNamedSecurityInfoW(private_key_file.c_str(),
                                              SE_FILE_OBJECT,
                                              DACL_SECURITY_INFORMATION,
                                              nullptr,
                                              nullptr,
                                              &dacl,
                                              nullptr,
                                              &descriptor);
    const auto descriptor_deleter = [](void* value)
    {
        if (value != nullptr)
        {
            LocalFree(value);
        }
    };
    std::unique_ptr<void, decltype(descriptor_deleter)> descriptor_owner(
        descriptor, descriptor_deleter);
    if (result != ERROR_SUCCESS)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "read client private-key ACL");
    }
    if (dacl == nullptr)
    {
        throw std::invalid_argument("client private key must not have an unrestricted DACL");
    }

    constexpr std::array<WELL_KNOWN_SID_TYPE, 4U> broad_principals{
        WinWorldSid, WinAuthenticatedUserSid, WinBuiltinUsersSid, WinBuiltinGuestsSid};
    std::array<std::array<unsigned char, SECURITY_MAX_SID_SIZE>, broad_principals.size()>
        sid_storage{};
    std::array<PSID, broad_principals.size()> broad_sids{};
    for (std::size_t index = 0U; index < broad_principals.size(); ++index)
    {
        DWORD sid_size = SECURITY_MAX_SID_SIZE;
        if (CreateWellKnownSid(broad_principals[index],
                               nullptr,
                               sid_storage[index].data(),
                               &sid_size) == FALSE)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "create ACL policy SID");
        }
        broad_sids[index] = sid_storage[index].data();
    }

    for (DWORD index = 0U; index < dacl->AceCount; ++index)
    {
        void* raw_ace = nullptr;
        if (GetAce(dacl, index, &raw_ace) == FALSE)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "inspect client private-key ACL entry");
        }
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
        {
            continue;
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        constexpr DWORD readable = FILE_READ_DATA | GENERIC_READ | GENERIC_ALL;
        if ((ace->Mask & readable) == 0U)
        {
            continue;
        }
        PSID sid = const_cast<DWORD*>(&ace->SidStart);
        for (const auto broad_sid : broad_sids)
        {
            if (EqualSid(sid, broad_sid) != FALSE)
            {
                throw std::invalid_argument(
                    "client private key ACL grants read access to a broad Windows principal");
            }
        }
    }
#else
    static_cast<void>(private_key_file);
#endif
}

} // namespace personal_vpn::client
