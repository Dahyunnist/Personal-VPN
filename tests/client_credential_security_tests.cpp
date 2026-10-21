#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#include <securitybaseapi.h>

#include "personal_vpn/credential_security.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace
{

void check_windows(const BOOL succeeded, const char* operation)
{
    if (succeeded == FALSE)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                operation);
    }
}

PSID current_user_sid(HANDLE& token, std::vector<unsigned char>& storage)
{
    check_windows(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token),
                  "open process token");
    DWORD required_size = 0U;
    static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0U, &required_size));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required_size == 0U)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "query process token size");
    }
    storage.resize(required_size);
    check_windows(GetTokenInformation(token,
                                      TokenUser,
                                      storage.data(),
                                      required_size,
                                      &required_size),
                  "read process token user");
    return reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid;
}

void set_test_acl(const std::wstring& path, PSID user_sid, PSID broad_sid,
                  const bool grant_broad_principal)
{
    std::array<EXPLICIT_ACCESSW, 2U> entries{};
    entries[0].grfAccessPermissions = FILE_ALL_ACCESS;
    entries[0].grfAccessMode = SET_ACCESS;
    entries[0].grfInheritance = NO_INHERITANCE;
    entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    entries[0].Trustee.ptstrName = static_cast<LPWSTR>(user_sid);
    DWORD entry_count = 1U;
    if (grant_broad_principal)
    {
        entries[1].grfAccessPermissions = FILE_GENERIC_READ;
        entries[1].grfAccessMode = SET_ACCESS;
        entries[1].grfInheritance = NO_INHERITANCE;
        entries[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entries[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entries[1].Trustee.ptstrName = static_cast<LPWSTR>(broad_sid);
        entry_count = 2U;
    }

    PACL dacl = nullptr;
    const auto acl_result = SetEntriesInAclW(entry_count, entries.data(), nullptr, &dacl);
    if (acl_result != ERROR_SUCCESS)
    {
        throw std::system_error(static_cast<int>(acl_result),
                                std::system_category(),
                                "build test private-key DACL");
    }
    const auto free_acl = [](PACL value)
    {
        if (value != nullptr)
        {
            LocalFree(value);
        }
    };
    std::unique_ptr<std::remove_pointer_t<PACL>, decltype(free_acl)> acl_owner(dacl, free_acl);
    const auto result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        dacl,
        nullptr);
    if (result != ERROR_SUCCESS)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "apply test private-key DACL");
    }
}

} // namespace

int main()
{
    HANDLE token = nullptr;
    const auto close_token = [](HANDLE value)
    {
        if (value != nullptr)
        {
            CloseHandle(value);
        }
    };
    std::unique_ptr<void, decltype(close_token)> token_owner(nullptr, close_token);
    std::wstring temporary_path(MAX_PATH, L'\0');
    const auto directory_length = GetTempPathW(static_cast<DWORD>(temporary_path.size()),
                                               temporary_path.data());
    if (directory_length == 0U || directory_length >= temporary_path.size())
    {
        std::cerr << "failed to locate temporary directory\n";
        return EXIT_FAILURE;
    }
    temporary_path.resize(directory_length);
    std::array<wchar_t, MAX_PATH> file_path{};
    if (GetTempFileNameW(temporary_path.c_str(), L"vpn", 0U, file_path.data()) == 0U)
    {
        std::cerr << "failed to create temporary private-key file\n";
        return EXIT_FAILURE;
    }
    const auto remove_file = [&file_path]() { static_cast<void>(DeleteFileW(file_path.data())); };

    try
    {
        std::vector<unsigned char> user_storage;
        const auto user_sid = current_user_sid(token, user_storage);
        token_owner.reset(token);

        const std::wstring path(file_path.data());

        constexpr char test_key_bytes[] = "validated private-key descriptor";
        HANDLE key_writer = CreateFileW(path.c_str(),
                                        GENERIC_WRITE,
                                        0U,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        if (key_writer == INVALID_HANDLE_VALUE)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "open test private key for writing");
        }
        DWORD bytes_written = 0U;
        const auto wrote_key = WriteFile(key_writer,
                                         test_key_bytes,
                                         static_cast<DWORD>(sizeof(test_key_bytes) - 1U),
                                         &bytes_written,
                                         nullptr);
        static_cast<void>(CloseHandle(key_writer));
        if (wrote_key == FALSE ||
            bytes_written != static_cast<DWORD>(sizeof(test_key_bytes) - 1U))
        {
            throw std::runtime_error("failed to write test private-key bytes");
        }

        set_test_acl(path, user_sid, nullptr, false);
        personal_vpn::client::validate_private_key_permissions(path);
        {
            auto validated_file =
                personal_vpn::client::open_validated_private_key_file(path);
            std::array<char, sizeof(test_key_bytes)> read_bytes{};
            const auto bytes_read = std::fread(
                read_bytes.data(), 1U, sizeof(test_key_bytes) - 1U, validated_file.get());
            if (bytes_read != sizeof(test_key_bytes) - 1U ||
                std::string(read_bytes.data(), bytes_read) !=
                    std::string(test_key_bytes, sizeof(test_key_bytes) - 1U))
            {
                throw std::runtime_error("validated private-key handle returned wrong bytes");
            }
            const auto writer_while_open = CreateFileW(path.c_str(),
                                                        GENERIC_WRITE,
                                                        FILE_SHARE_READ,
                                                        nullptr,
                                                        OPEN_EXISTING,
                                                        FILE_ATTRIBUTE_NORMAL,
                                                        nullptr);
            if (writer_while_open != INVALID_HANDLE_VALUE)
            {
                static_cast<void>(CloseHandle(writer_while_open));
                throw std::runtime_error(
                    "validated private-key handle allowed concurrent replacement");
            }
            const auto writer_error = GetLastError();
            if (writer_error != ERROR_SHARING_VIOLATION && writer_error != ERROR_ACCESS_DENIED)
            {
                throw std::system_error(static_cast<int>(writer_error),
                                        std::system_category(),
                                        "verify private-key replacement is denied");
            }
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
        for (const auto principal : broad_principals)
        {
            std::array<unsigned char, SECURITY_MAX_SID_SIZE> sid_storage{};
            DWORD sid_size = static_cast<DWORD>(sid_storage.size());
            check_windows(CreateWellKnownSid(principal, nullptr, sid_storage.data(), &sid_size),
                          "create broad-principal SID");
            set_test_acl(path, user_sid, sid_storage.data(), true);
            bool rejected = false;
            try
            {
                static_cast<void>(personal_vpn::client::open_validated_private_key_file(path));
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            if (!rejected)
            {
                throw std::runtime_error("private-key read was allowed for a broad principal");
            }
        }
        remove_file();
        std::cout << "Windows private-key ACL checks passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        remove_file();
        std::cerr << "Windows private-key ACL test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
