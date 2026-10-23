#include "personal_vpn/windows_broker_profile_store.hpp"

#ifdef _WIN32

#include "personal_vpn/broker_protocol.hpp"
#include "personal_vpn/client_config.hpp"

#include <Windows.h>
#include <Aclapi.h>
#include <Objbase.h>
#include <sddl.h>
#include <userenv.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace personal_vpn::broker
{
namespace
{

constexpr std::uintmax_t maximum_credential_bytes = 8U * 1024U * 1024U;

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

[[noreturn]] void fail(const char* operation)
{
    throw std::system_error(static_cast<int>(GetLastError()),
                            std::system_category(),
                            operation);
}

struct CurrentUser
{
    std::unique_ptr<void, HandleCloser> token;
    std::vector<std::uint8_t> storage;
    PSID sid{nullptr};
    std::wstring sid_string;
    std::filesystem::path profile_path;

    CurrentUser()
    {
        HANDLE raw_token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token))
        {
            fail("open profile import user token");
        }
        token.reset(raw_token);
        DWORD required = 0U;
        static_cast<void>(GetTokenInformation(raw_token, TokenUser, nullptr, 0U, &required));
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0U)
        {
            fail("query profile import user token size");
        }
        storage.resize(required);
        if (!GetTokenInformation(raw_token, TokenUser, storage.data(), required, &required))
        {
            fail("read profile import user token");
        }
        sid = reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid;

        LPWSTR raw_sid = nullptr;
        if (!ConvertSidToStringSidW(sid, &raw_sid))
        {
            fail("format profile import user SID");
        }
        std::unique_ptr<wchar_t, LocalFreeDeleter> sid_owner(raw_sid);
        sid_string = raw_sid;

        DWORD profile_length = 0U;
        static_cast<void>(GetUserProfileDirectoryW(raw_token, nullptr, &profile_length));
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || profile_length < 2U ||
            profile_length > 32'768U)
        {
            fail("query profile import user profile path size");
        }
        std::vector<wchar_t> profile_buffer(profile_length);
        if (!GetUserProfileDirectoryW(raw_token, profile_buffer.data(), &profile_length))
        {
            fail("read profile import user profile path");
        }
        profile_path = profile_buffer.data();
    }
};

void apply_private_acl(const std::filesystem::path& path,
                       const CurrentUser& user,
                       const bool directory)
{
    const std::wstring sddl = directory
                                  ? L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" +
                                        user.sid_string + L")"
                                  : L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" +
                                        user.sid_string + L")";
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &raw_descriptor, nullptr))
    {
        fail("build private broker profile ACL");
    }
    std::unique_ptr<void, LocalFreeDeleter> descriptor(raw_descriptor);
    BOOL dacl_present = FALSE;
    BOOL dacl_defaulted = FALSE;
    PACL dacl = nullptr;
    if (!GetSecurityDescriptorDacl(raw_descriptor, &dacl_present, &dacl, &dacl_defaulted) ||
        !dacl_present || dacl == nullptr)
    {
        throw std::runtime_error("private broker profile DACL is unavailable");
    }
    const auto result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()),
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            PROTECTED_DACL_SECURITY_INFORMATION,
        user.sid,
        nullptr,
        dacl,
        nullptr);
    if (result != ERROR_SUCCESS)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "protect broker profile path");
    }
}

void ensure_profile_root(const std::filesystem::path& profile_path,
                         const CurrentUser& user)
{
    auto directory = profile_path;
    const std::array<std::filesystem::path, 4U> components{
        L"AppData", L"Local", L"PersonalVPN", L"profiles"};
    for (const auto& component : components)
    {
        directory /= component;
        std::error_code error;
        if (!std::filesystem::exists(directory, error))
        {
            static_cast<void>(std::filesystem::create_directory(directory, error));
            if (error)
            {
                throw std::system_error(error, "create broker profile directory");
            }
        }
        const auto attributes = GetFileAttributesW(directory.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
        {
            throw std::invalid_argument(
                "broker profile path must contain only real directories");
        }
    }
    apply_private_acl(directory, user, true);
}

std::filesystem::path existing_profile_root(const CurrentUser& user)
{
    auto directory = user.profile_path;
    const std::array<std::filesystem::path, 4U> components{
        L"AppData", L"Local", L"PersonalVPN", L"profiles"};
    for (const auto& component : components)
    {
        directory /= component;
        const auto attributes = GetFileAttributesW(directory.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                return {};
            }
            throw std::system_error(static_cast<int>(error),
                                    std::system_category(),
                                    "inspect broker profile directory");
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
        {
            throw std::invalid_argument("broker profile path must contain only real directories");
        }
    }
    return directory;
}

std::string new_profile_id()
{
    GUID guid{};
    const auto result = CoCreateGuid(&guid);
    if (result != S_OK)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "create broker profile ID");
    }
    std::array<char, 34U> output{};
    const auto written = std::snprintf(output.data(),
                                       output.size(),
                                       "p%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                                       static_cast<unsigned long>(guid.Data1),
                                       static_cast<unsigned int>(guid.Data2),
                                       static_cast<unsigned int>(guid.Data3),
                                       static_cast<unsigned int>(guid.Data4[0]),
                                       static_cast<unsigned int>(guid.Data4[1]),
                                       static_cast<unsigned int>(guid.Data4[2]),
                                       static_cast<unsigned int>(guid.Data4[3]),
                                       static_cast<unsigned int>(guid.Data4[4]),
                                       static_cast<unsigned int>(guid.Data4[5]),
                                       static_cast<unsigned int>(guid.Data4[6]),
                                       static_cast<unsigned int>(guid.Data4[7]));
    if (written != 33)
    {
        throw std::runtime_error("format broker profile ID failed");
    }
    const std::string profile_id(output.data());
    if (!is_valid_profile_id(profile_id))
    {
        throw std::runtime_error("generated broker profile ID is invalid");
    }
    return profile_id;
}

void secure_copy_file(const std::filesystem::path& source,
                      const std::filesystem::path& destination,
                      const CurrentUser& user)
{
    const auto source_handle = CreateFileW(source.c_str(),
                                           GENERIC_READ,
                                           FILE_SHARE_READ,
                                           nullptr,
                                           OPEN_EXISTING,
                                           FILE_FLAG_OPEN_REPARSE_POINT |
                                               FILE_FLAG_SEQUENTIAL_SCAN,
                                           nullptr);
    if (source_handle == INVALID_HANDLE_VALUE)
    {
        fail("open broker profile credential source");
    }
    std::unique_ptr<void, HandleCloser> source_owner(source_handle);
    FILE_ATTRIBUTE_TAG_INFO source_info{};
    LARGE_INTEGER source_size{};
    if (!GetFileInformationByHandleEx(source_handle,
                                      FileAttributeTagInfo,
                                      &source_info,
                                      sizeof(source_info)) ||
        (source_info.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
            0U ||
        GetFileType(source_handle) != FILE_TYPE_DISK ||
        !GetFileSizeEx(source_handle, &source_size) || source_size.QuadPart <= 0 ||
        static_cast<std::uint64_t>(source_size.QuadPart) > maximum_credential_bytes)
    {
        throw std::invalid_argument("broker profile credential must be a regular file up to 8 MiB");
    }

    const std::wstring sddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + user.sid_string + L")";
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &raw_descriptor, nullptr))
    {
        fail("build private broker credential ACL");
    }
    std::unique_ptr<void, LocalFreeDeleter> descriptor(raw_descriptor);
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = raw_descriptor;
    const auto destination_handle = CreateFileW(destination.c_str(),
                                                GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
                                                0U,
                                                &attributes,
                                                CREATE_NEW,
                                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                                nullptr);
    if (destination_handle == INVALID_HANDLE_VALUE)
    {
        fail("create private broker credential");
    }
    std::unique_ptr<void, HandleCloser> destination_owner(destination_handle);
    try
    {
        std::array<std::uint8_t, 16U * 1024U> buffer{};
        std::uint64_t copied = 0U;
        while (copied < static_cast<std::uint64_t>(source_size.QuadPart))
        {
            DWORD read = 0U;
            const auto remaining = static_cast<std::uint64_t>(source_size.QuadPart) - copied;
            const auto request = static_cast<DWORD>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            if (!ReadFile(source_handle, buffer.data(), request, &read, nullptr) || read == 0U)
            {
                fail("read broker profile credential");
            }
            DWORD written = 0U;
            if (!WriteFile(destination_handle, buffer.data(), read, &written, nullptr) ||
                written != read)
            {
                fail("write private broker credential");
            }
            copied += read;
        }
        if (!FlushFileBuffers(destination_handle))
        {
            fail("flush private broker credential");
        }
    }
    catch (...)
    {
        destination_owner.reset();
        static_cast<void>(DeleteFileW(destination.c_str()));
        throw;
    }
}

void write_private_config(const std::filesystem::path& destination,
                          const std::string& contents,
                          const CurrentUser& user)
{
    const std::wstring sddl = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + user.sid_string + L")";
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &raw_descriptor, nullptr))
    {
        fail("build private broker profile config ACL");
    }
    std::unique_ptr<void, LocalFreeDeleter> descriptor(raw_descriptor);
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = raw_descriptor;
    const auto handle = CreateFileW(destination.c_str(),
                                    GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
                                    0U,
                                    &attributes,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                    nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        fail("create private broker profile config");
    }
    std::unique_ptr<void, HandleCloser> owner(handle);
    std::size_t offset = 0U;
    while (offset < contents.size())
    {
        const auto remaining = contents.size() - offset;
        const auto request = static_cast<DWORD>(std::min<std::size_t>(remaining, 16U * 1024U));
        DWORD written = 0U;
        if (!WriteFile(handle, contents.data() + offset, request, &written, nullptr) || written == 0U)
        {
            owner.reset();
            static_cast<void>(DeleteFileW(destination.c_str()));
            fail("write private broker profile config");
        }
        offset += written;
    }
    if (!FlushFileBuffers(handle))
    {
        owner.reset();
        static_cast<void>(DeleteFileW(destination.c_str()));
        fail("flush private broker profile config");
    }
}

} // namespace

StoredBrokerProfile store_broker_profile(const std::filesystem::path& source_config)
{
    const auto source_config_data = client::load_client_config(source_config);
    const auto source = std::filesystem::absolute(source_config).lexically_normal();
    boost::property_tree::ptree document;
    std::ifstream source_stream(source, std::ios::binary);
    if (!source_stream)
    {
        throw std::runtime_error("open source broker profile failed");
    }
    boost::property_tree::read_json(source_stream, document);

    CurrentUser user;
    const auto profile_id = new_profile_id();
    const auto profile_directory = user.profile_path / L"AppData" / L"Local" / L"PersonalVPN" /
                                   L"profiles";
    ensure_profile_root(user.profile_path, user);

    const auto ca_name = profile_id + "-ca.crt";
    const auto certificate_name = profile_id + "-client.crt";
    const auto key_name = profile_id + "-client.key";
    document.put("tls.ca_file", ca_name);
    document.put("tls.certificate_chain_file", certificate_name);
    document.put("tls.private_key_file", key_name);
    std::string crl_name;
    if (!source_config_data.server_crl_file.empty())
    {
        crl_name = profile_id + "-server.crl";
        document.put("tls.server_crl_file", crl_name);
    }
    std::ostringstream serialized;
    boost::property_tree::write_json(serialized, document, false);

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files{
        {source_config_data.ca_file, profile_directory / ca_name},
        {source_config_data.certificate_chain_file, profile_directory / certificate_name},
        {source_config_data.private_key_file, profile_directory / key_name},
    };
    if (!source_config_data.server_crl_file.empty())
    {
        files.emplace_back(source_config_data.server_crl_file, profile_directory / crl_name);
    }
    std::vector<std::filesystem::path> created_files;
    try
    {
        for (const auto& [input, output] : files)
        {
            secure_copy_file(input, output, user);
            created_files.push_back(output);
        }
        const auto config_path = profile_directory / (profile_id + ".json");
        write_private_config(config_path, serialized.str(), user);
        created_files.push_back(config_path);
        static_cast<void>(client::load_client_config(config_path));
        return {profile_id, config_path};
    }
    catch (...)
    {
        for (const auto& path : created_files)
        {
            static_cast<void>(DeleteFileW(path.c_str()));
        }
        throw;
    }
}

void delete_broker_profile(const std::string& profile_id)
{
    if (!is_valid_profile_id(profile_id))
    {
        throw std::invalid_argument("broker profile ID is invalid");
    }
    const CurrentUser user;
    const auto directory = existing_profile_root(user);
    if (directory.empty())
    {
        return;
    }
    const std::array<std::string, 5U> suffixes{
        "-ca.crt", "-client.crt", "-client.key", "-server.crl", ".json"};
    for (const auto& suffix : suffixes)
    {
        const auto path = directory / (profile_id + suffix);
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                continue;
            }
            throw std::system_error(static_cast<int>(error),
                                    std::system_category(),
                                    "inspect broker profile file");
        }
        if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U)
        {
            throw std::invalid_argument("broker profile files must be regular files");
        }
        if (!DeleteFileW(path.c_str()))
        {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                continue;
            }
            throw std::system_error(static_cast<int>(error),
                                    std::system_category(),
                                    "delete broker profile file");
        }
    }
}

} // namespace personal_vpn::broker

#endif
