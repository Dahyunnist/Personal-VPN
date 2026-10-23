#include "personal_vpn/broker_service_policy.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace personal_vpn::broker
{
namespace
{

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

struct LocalFreeDeleter
{
    void operator()(void* value) const noexcept
    {
        if (value != nullptr)
        {
            static_cast<void>(LocalFree(value));
        }
    }
};

struct TrustedSids
{
    std::array<std::array<std::uint8_t, SECURITY_MAX_SID_SIZE>, 2U> storage{};
    std::array<PSID, 2U> values{};

    TrustedSids()
    {
        constexpr std::array<WELL_KNOWN_SID_TYPE, 2U> types{
            WinLocalSystemSid, WinBuiltinAdministratorsSid};
        for (std::size_t index = 0U; index < types.size(); ++index)
        {
            DWORD size = static_cast<DWORD>(storage[index].size());
            if (!CreateWellKnownSid(types[index], nullptr, storage[index].data(), &size))
            {
                throw std::system_error(static_cast<int>(GetLastError()),
                                        std::system_category(),
                                        "create trusted service-policy SID");
            }
            values[index] = storage[index].data();
        }
    }

    [[nodiscard]] bool contains(PSID sid) const noexcept
    {
        for (const auto trusted : values)
        {
            if (EqualSid(sid, trusted))
            {
                return true;
            }
        }
        return false;
    }
};

void validate_policy_acl(const HANDLE handle)
{
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto result = GetSecurityInfo(handle,
                                        SE_FILE_OBJECT,
                                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                        &owner,
                                        nullptr,
                                        &dacl,
                                        nullptr,
                                        &descriptor);
    std::unique_ptr<void, LocalFreeDeleter> descriptor_owner(descriptor);
    if (result != ERROR_SUCCESS)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "read broker policy ACL");
    }
    if (owner == nullptr || dacl == nullptr)
    {
        throw std::invalid_argument("broker policy owner and DACL must be explicit");
    }
    const TrustedSids trusted;
    if (!trusted.contains(owner))
    {
        throw std::invalid_argument("broker policy must be owned by SYSTEM or Administrators");
    }

    constexpr DWORD write_access = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA |
                                   FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                   DELETE | FILE_DELETE_CHILD | WRITE_DAC | WRITE_OWNER |
                                   FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
    for (DWORD index = 0U; index < dacl->AceCount; ++index)
    {
        void* raw_ace = nullptr;
        if (!GetAce(dacl, index, &raw_ace))
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "inspect broker policy ACL");
        }
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE)
        {
            continue;
        }
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
        {
            throw std::invalid_argument("broker policy ACL contains an unsupported ACE");
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        if ((ace->Mask & write_access) == 0U)
        {
            continue;
        }
        PSID sid = const_cast<DWORD*>(&ace->SidStart);
        if (!trusted.contains(sid))
        {
            throw std::invalid_argument(
                "broker policy grants write access outside SYSTEM and Administrators");
        }
    }
}

std::vector<char> read_policy_file(const std::filesystem::path& policy_file)
{
    if (!policy_file.is_absolute() || policy_file.parent_path().empty())
    {
        throw std::invalid_argument("broker policy path must be absolute");
    }
    const auto directory = CreateFileW(policy_file.parent_path().c_str(),
                                       FILE_READ_ATTRIBUTES | READ_CONTROL,
                                       FILE_SHARE_READ,
                                       nullptr,
                                       OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                       nullptr);
    if (directory == INVALID_HANDLE_VALUE)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "open broker policy directory");
    }
    std::unique_ptr<void, HandleCloser> directory_owner(directory);
    FILE_ATTRIBUTE_TAG_INFO directory_info{};
    if (!GetFileInformationByHandleEx(directory,
                                      FileAttributeTagInfo,
                                      &directory_info,
                                      sizeof(directory_info)) ||
        (directory_info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (directory_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
    {
        throw std::invalid_argument("broker policy directory must not be a reparse point");
    }
    validate_policy_acl(directory);

    const auto file = CreateFileW(policy_file.c_str(),
                                  GENERIC_READ | READ_CONTROL,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
                                  nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "open broker policy file");
    }
    std::unique_ptr<void, HandleCloser> file_owner(file);
    FILE_ATTRIBUTE_TAG_INFO file_info{};
    LARGE_INTEGER file_size{};
    if (!GetFileInformationByHandleEx(file,
                                      FileAttributeTagInfo,
                                      &file_info,
                                      sizeof(file_info)) ||
        (file_info.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
            0U ||
        GetFileType(file) != FILE_TYPE_DISK || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart <= 0 || file_size.QuadPart > 64LL * 1024LL)
    {
        throw std::invalid_argument("broker policy must be a regular file up to 64 KiB");
    }
    validate_policy_acl(file);
    std::vector<char> contents(static_cast<std::size_t>(file_size.QuadPart));
    std::size_t offset = 0U;
    while (offset < contents.size())
    {
        const auto remaining = contents.size() - offset;
        const auto request_size = static_cast<DWORD>(remaining);
        DWORD read = 0U;
        if (!ReadFile(file, contents.data() + offset, request_size, &read, nullptr) || read == 0U)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "read broker policy file");
        }
        offset += read;
    }
    return contents;
}

} // namespace

client::ClientNetworkPolicy load_broker_network_policy(
    const std::filesystem::path& policy_file)
{
    const auto contents = read_policy_file(policy_file);
    return parse_broker_network_policy(std::string_view(contents.data(), contents.size()));
}

} // namespace personal_vpn::broker

#endif
