#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#include <securitybaseapi.h>

#include "personal_vpn/credential_security.hpp"
#include "personal_vpn/client_tls_security.hpp"
#include "personal_vpn/client_config.hpp"
#include "personal_vpn/windows_broker_profile_store.hpp"
#include "personal_vpn/broker_protocol.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

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

void write_test_identity(const std::wstring& key_path, const std::wstring& certificate_path)
{
    using KeyContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;

    KeyContext key_context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), &EVP_PKEY_CTX_free);
    if (!key_context || EVP_PKEY_keygen_init(key_context.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), 2048) <= 0)
    {
        throw std::runtime_error("failed to initialize test private-key generation");
    }
    EVP_PKEY* raw_key = nullptr;
    if (EVP_PKEY_keygen(key_context.get(), &raw_key) <= 0)
    {
        throw std::runtime_error("failed to generate test private key");
    }
    Key key(raw_key, &EVP_PKEY_free);

    Certificate certificate(X509_new(), &X509_free);
    if (!certificate || X509_set_version(certificate.get(), 2L) != 1 ||
        ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1L) != 1 ||
        X509_gmtime_adj(X509_get_notBefore(certificate.get()), -60L) == nullptr ||
        X509_gmtime_adj(X509_get_notAfter(certificate.get()), 3'600L) == nullptr ||
        X509_set_pubkey(certificate.get(), key.get()) != 1)
    {
        throw std::runtime_error("failed to initialize test certificate");
    }
    auto* subject = X509_get_subject_name(certificate.get());
    constexpr char common_name[] = "vpn.example.test";
    if (subject == nullptr ||
        X509_NAME_add_entry_by_txt(subject,
                                   "CN",
                                   MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>(common_name),
                                   -1,
                                   -1,
                                   0) != 1 ||
        X509_set_issuer_name(certificate.get(), subject) != 1 ||
        X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0)
    {
        throw std::runtime_error("failed to sign test certificate");
    }

    const auto key_path_string = std::filesystem::path(key_path).string();
    const auto certificate_path_string = std::filesystem::path(certificate_path).string();
    using Bio = std::unique_ptr<BIO, decltype(&BIO_free)>;
    Bio key_file(BIO_new_file(key_path_string.c_str(), "wb"), &BIO_free);
    Bio certificate_file(BIO_new_file(certificate_path_string.c_str(), "wb"), &BIO_free);
    if (!key_file || !certificate_file ||
        PEM_write_bio_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) !=
            1 ||
        PEM_write_bio_X509(certificate_file.get(), certificate.get()) != 1)
    {
        throw std::runtime_error("failed to write test PEM identity");
    }
}

void write_client_profile(const std::filesystem::path& path,
                          const std::string& certificate_name,
                          const std::string& key_name,
                          const bool add_unknown_field,
                          const std::string& routes = "[\"10.20.0.0/16\"]",
                          const bool allow_default_route = false)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("failed to create test client profile");
    }
    output << "{\n"
              "  \"schema_version\": 1,\n"
              "  \"server\": {\"host\": \"127.0.0.1\", \"port\": 8443, "
              "\"expected_name\": \"vpn.example.test\"},\n"
              "  \"tls\": {\"ca_file\": \""
           << certificate_name << "\", \"certificate_chain_file\": \"" << certificate_name
           << "\", \"private_key_file\": \"" << key_name << "\"},\n"
              "  \"tunnel\": {\"requested_mtu\": 1400, \"routes\": "
           << routes << ", \"allow_default_route\": "
           << (allow_default_route ? "true" : "false")
           << "}"
           << (add_unknown_field ? ",\n  \"unexpected\": true" : "") << "\n}\n";
    if (!output)
    {
        throw std::runtime_error("failed to write test client profile");
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
    std::array<wchar_t, MAX_PATH> certificate_path{};
    if (GetTempFileNameW(temporary_path.c_str(), L"vpn", 0U, certificate_path.data()) == 0U)
    {
        remove_file();
        std::cerr << "failed to create temporary test certificate file\n";
        return EXIT_FAILURE;
    }
    const auto remove_certificate = [&certificate_path]()
    { static_cast<void>(DeleteFileW(certificate_path.data())); };
    std::array<wchar_t, MAX_PATH> oversized_key_path{};
    if (GetTempFileNameW(temporary_path.c_str(), L"vpn", 0U, oversized_key_path.data()) == 0U)
    {
        remove_file();
        remove_certificate();
        std::cerr << "failed to create temporary oversized-key file\n";
        return EXIT_FAILURE;
    }
    const auto remove_oversized_key = [&oversized_key_path]()
    { static_cast<void>(DeleteFileW(oversized_key_path.data())); };
    std::array<wchar_t, MAX_PATH> profile_path{};
    if (GetTempFileNameW(temporary_path.c_str(), L"vpn", 0U, profile_path.data()) == 0U)
    {
        remove_file();
        remove_certificate();
        remove_oversized_key();
        std::cerr << "failed to create temporary test profile\n";
        return EXIT_FAILURE;
    }
    const auto remove_profile = [&profile_path]()
    { static_cast<void>(DeleteFileW(profile_path.data())); };

    try
    {
        std::vector<unsigned char> user_storage;
        const auto user_sid = current_user_sid(token, user_storage);
        token_owner.reset(token);

        const std::wstring path(file_path.data());
        const std::wstring certificate(certificate_path.data());
        const auto key_path_string = std::filesystem::path(path).string();
        const auto certificate_path_string = std::filesystem::path(certificate).string();
        write_test_identity(path, certificate);

        set_test_acl(path, user_sid, nullptr, false);
        personal_vpn::client::validate_private_key_permissions(path);
        HANDLE impersonation_source = nullptr;
        check_windows(OpenProcessToken(GetCurrentProcess(),
                                       TOKEN_QUERY | TOKEN_DUPLICATE,
                                       &impersonation_source),
                      "open token for impersonation test");
        std::unique_ptr<void, decltype(close_token)> impersonation_source_owner(
            impersonation_source, close_token);
        HANDLE impersonation_token = nullptr;
        check_windows(DuplicateTokenEx(impersonation_source,
                                       TOKEN_QUERY | TOKEN_IMPERSONATE,
                                       nullptr,
                                       SecurityImpersonation,
                                       TokenImpersonation,
                                       &impersonation_token),
                      "duplicate token for impersonation test");
        std::unique_ptr<void, decltype(close_token)> impersonation_token_owner(
            impersonation_token, close_token);
        check_windows(SetThreadToken(nullptr, impersonation_token),
                      "set thread token for private-key validation");
        try
        {
            personal_vpn::client::validate_private_key_permissions(path);
        }
        catch (...)
        {
            static_cast<void>(RevertToSelf());
            throw;
        }
        check_windows(RevertToSelf(), "revert private-key validation impersonation");
        const std::filesystem::path profile(profile_path.data());
        const auto certificate_name = std::filesystem::path(certificate).filename().generic_string();
        const auto key_name = std::filesystem::path(path).filename().generic_string();
        write_client_profile(profile, certificate_name, key_name, false);
        auto client_config = personal_vpn::client::load_client_config(profile);
        if (client_config.server_host != "127.0.0.1" || client_config.server_port != 8'443U ||
            client_config.expected_server_name != "vpn.example.test" ||
            client_config.routes != std::vector<std::string>{"10.20.0.0/16"} ||
            client_config.allow_default_route)
        {
            throw std::runtime_error("Windows profile parser returned incorrect settings");
        }
        auto tls_context = personal_vpn::client::make_client_tls_context(client_config);
        if (tls_context.native_handle() == nullptr)
        {
            throw std::runtime_error("TLS context did not load the profile's validated key");
        }
        const auto stored_profile = personal_vpn::broker::store_broker_profile(profile);
        if (!personal_vpn::broker::is_valid_profile_id(stored_profile.profile_id) ||
            stored_profile.config_path.filename() !=
                std::filesystem::path(stored_profile.profile_id + ".json"))
        {
            throw std::runtime_error("broker profile store returned an invalid profile identity");
        }
        const auto stored_config =
            personal_vpn::client::load_client_config(stored_profile.config_path);
        if (stored_config.server_host != client_config.server_host ||
            stored_config.server_port != client_config.server_port ||
            stored_config.routes != client_config.routes ||
            stored_config.private_key_file.filename() !=
                std::filesystem::path(stored_profile.profile_id + "-client.key"))
        {
            throw std::runtime_error("stored broker profile or copied credentials are invalid");
        }
        auto stored_tls_context =
            personal_vpn::client::make_client_tls_context(stored_config);
        if (stored_tls_context.native_handle() == nullptr)
        {
            throw std::runtime_error("copied broker credentials did not load into TLS");
        }
        const auto stored_directory = stored_profile.config_path.parent_path();
        personal_vpn::broker::delete_broker_profile(stored_profile.profile_id);
        for (const auto* suffix : {"-ca.crt", "-client.crt", "-client.key", ".json"})
        {
            if (GetFileAttributesW(
                    (stored_directory / (stored_profile.profile_id + suffix)).c_str()) !=
                INVALID_FILE_ATTRIBUTES)
            {
                throw std::runtime_error("profile deletion left a copied credential file");
            }
        }
        personal_vpn::broker::delete_broker_profile(stored_profile.profile_id);
        bool invalid_profile_delete_rejected = false;
        try
        {
            personal_vpn::broker::delete_broker_profile("../outside");
        }
        catch (const std::invalid_argument&)
        {
            invalid_profile_delete_rejected = true;
        }
        if (!invalid_profile_delete_rejected)
        {
            throw std::runtime_error("profile deletion accepted a path-like profile ID");
        }
        const personal_vpn::client::ClientNetworkPolicy network_policy{
            {"10.20.0.0/16"}, false};
        client_config = personal_vpn::client::load_client_config(profile, network_policy);
        if (client_config.routes != std::vector<std::string>{"10.20.0.0/16"})
        {
            throw std::runtime_error("trusted network policy rejected its authorized route");
        }
        write_client_profile(profile, certificate_name, key_name, false,
                             "[\"10.30.0.0/16\"]");
        bool unauthorized_route_rejected = false;
        try
        {
            static_cast<void>(personal_vpn::client::load_client_config(profile, network_policy));
        }
        catch (const std::invalid_argument&)
        {
            unauthorized_route_rejected = true;
        }
        if (!unauthorized_route_rejected)
        {
            throw std::runtime_error("trusted network policy accepted a user-selected route");
        }
        write_client_profile(profile, certificate_name, key_name, true);
        bool unknown_profile_field_rejected = false;
        try
        {
            static_cast<void>(personal_vpn::client::load_client_config(profile));
        }
        catch (const std::invalid_argument&)
        {
            unknown_profile_field_rejected = true;
        }
        if (!unknown_profile_field_rejected)
        {
            throw std::runtime_error("Windows profile parser accepted an unknown root field");
        }

        const std::wstring oversized_key(oversized_key_path.data());
        const auto oversized_key_string = std::filesystem::path(oversized_key).string();
        HANDLE oversized_writer = CreateFileW(oversized_key.c_str(),
                                              GENERIC_WRITE,
                                              0U,
                                              nullptr,
                                              OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL,
                                              nullptr);
        if (oversized_writer == INVALID_HANDLE_VALUE)
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "open oversized test key for writing");
        }
        LARGE_INTEGER oversized_position{};
        oversized_position.QuadPart = 1024LL * 1024LL + 1LL;
        const auto moved = SetFilePointerEx(
            oversized_writer, oversized_position, nullptr, FILE_BEGIN);
        const auto sized = moved != FALSE && SetEndOfFile(oversized_writer) != FALSE;
        const auto resize_error = sized ? ERROR_SUCCESS : GetLastError();
        static_cast<void>(CloseHandle(oversized_writer));
        if (!sized)
        {
            throw std::system_error(static_cast<int>(resize_error),
                                    std::system_category(),
                                    "create oversized test private key");
        }
        set_test_acl(oversized_key, user_sid, nullptr, false);
        bool oversized_key_rejected = false;
        try
        {
            static_cast<void>(personal_vpn::client::make_client_tls_context(
                personal_vpn::client::ClientTlsConfig{certificate_path_string,
                                                      certificate_path_string,
                                                      oversized_key_string,
                                                      "vpn.example.test"}));
        }
        catch (const std::invalid_argument&)
        {
            oversized_key_rejected = true;
        }
        if (!oversized_key_rejected)
        {
            throw std::runtime_error("client TLS loader accepted a private key larger than 1 MiB");
        }

        {
            auto validated_file =
                personal_vpn::client::open_validated_private_key_file(path);
            std::array<char, 32U> read_bytes{};
            if (std::fread(read_bytes.data(), 1U, read_bytes.size(), validated_file.get()) == 0U)
            {
                throw std::runtime_error("validated private-key handle returned no PEM bytes");
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
                static_cast<void>(personal_vpn::client::make_client_tls_context(
                    personal_vpn::client::ClientTlsConfig{certificate_path_string,
                                                          certificate_path_string,
                                                          key_path_string,
                                                          "vpn.example.test"}));
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

        PSID unauthorized_sid = nullptr;
        check_windows(ConvertStringSidToSidW(
                          L"S-1-5-21-1111111111-2222222222-3333333333-4444",
                          &unauthorized_sid),
                      "create unauthorized user SID");
        const auto free_sid = [](void* value)
        {
            if (value != nullptr)
            {
                LocalFree(value);
            }
        };
        std::unique_ptr<void, decltype(free_sid)> unauthorized_sid_owner(
            unauthorized_sid, free_sid);
        set_test_acl(path, user_sid, unauthorized_sid, true);
        bool unauthorized_principal_rejected = false;
        try
        {
            static_cast<void>(personal_vpn::client::make_client_tls_context(
                personal_vpn::client::ClientTlsConfig{certificate_path_string,
                                                      certificate_path_string,
                                                      key_path_string,
                                                      "vpn.example.test"}));
        }
        catch (const std::invalid_argument&)
        {
            unauthorized_principal_rejected = true;
        }
        if (!unauthorized_principal_rejected)
        {
            throw std::runtime_error(
                "private-key read was allowed for an unapproved user SID");
        }

        remove_file();
        remove_certificate();
        remove_oversized_key();
        remove_profile();
        std::cout << "Windows private-key ACL checks passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        remove_file();
        remove_certificate();
        remove_oversized_key();
        remove_profile();
        std::cerr << "Windows private-key ACL test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
