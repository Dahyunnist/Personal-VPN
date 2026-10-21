#include "personal_vpn/credential_security.hpp"

#include <cerrno>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Aclapi.h>

#include <array>
#include <cstdint>
#include <io.h>
#include <memory>
#include <fcntl.h>
#endif

namespace personal_vpn::client
{
namespace
{

#if defined(_WIN32)
struct HandleCloser
{
    void operator()(void* value) const noexcept
    {
        if (value != nullptr && value != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value));
        }
    }
};

using HandlePtr = std::unique_ptr<void, HandleCloser>;

void validate_private_key_acl(HANDLE handle)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto result = GetSecurityInfo(handle,
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

    constexpr std::array<WELL_KNOWN_SID_TYPE, 11U> broad_principals{
        WinWorldSid,
        WinAuthenticatedUserSid,
        WinBuiltinUsersSid,
        WinBuiltinGuestsSid,
        WinInteractiveSid,
        WinNetworkSid,
        WinBatchSid,
        WinServiceSid,
        WinAnonymousSid,
        WinTerminalServerSid,
        WinRestrictedCodeSid};
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
}
#endif

} // namespace

ValidatedPrivateKeyFile open_validated_private_key_file(
    const std::filesystem::path& private_key_file)
{
#if defined(_WIN32)
    const auto raw_handle = CreateFileW(private_key_file.c_str(),
                                        GENERIC_READ | READ_CONTROL,
                                        FILE_SHARE_READ,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                        nullptr);
    if (raw_handle == INVALID_HANDLE_VALUE)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "open client private key");
    }
    HandlePtr handle_owner(raw_handle);

    FILE_ATTRIBUTE_TAG_INFO file_info{};
    if (GetFileInformationByHandleEx(raw_handle,
                                     FileAttributeTagInfo,
                                     &file_info,
                                     static_cast<DWORD>(sizeof(file_info))) == FALSE)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "inspect client private key");
    }
    if ((file_info.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) !=
        0U ||
        GetFileType(raw_handle) != FILE_TYPE_DISK)
    {
        throw std::invalid_argument("client private key must be a regular non-reparse file");
    }
    validate_private_key_acl(raw_handle);

    const auto descriptor = _open_osfhandle(reinterpret_cast<std::intptr_t>(raw_handle),
                                             _O_RDONLY | _O_BINARY);
    if (descriptor < 0)
    {
        throw std::system_error(errno,
                                std::generic_category(),
                                "create client private-key stream");
    }
    static_cast<void>(handle_owner.release());

    auto* stream = _fdopen(descriptor, "rb");
    if (stream == nullptr)
    {
        const auto error = errno;
        static_cast<void>(_close(descriptor));
        throw std::system_error(error,
                                std::generic_category(),
                                "read client private key");
    }
    return ValidatedPrivateKeyFile(stream);
#else
    auto* stream = std::fopen(private_key_file.string().c_str(), "rb");
    if (stream == nullptr)
    {
        throw std::system_error(errno,
                                std::generic_category(),
                                "open client private key");
    }
    return ValidatedPrivateKeyFile(stream);
#endif
}

void validate_private_key_permissions(const std::filesystem::path& private_key_file)
{
    static_cast<void>(open_validated_private_key_file(private_key_file));
}

} // namespace personal_vpn::client
