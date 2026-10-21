#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#include <securitybaseapi.h>

#include "personal_vpn/credential_security.hpp"
#include "personal_vpn/client_tls_security.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
        auto tls_context = personal_vpn::client::make_client_tls_context(
            personal_vpn::client::ClientTlsConfig{certificate_path_string,
                                                  certificate_path_string,
                                                  key_path_string,
                                                  "vpn.example.test"});
        if (tls_context.native_handle() == nullptr)
        {
            throw std::runtime_error("TLS context did not load the validated key handle");
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
        remove_file();
        remove_certificate();
        std::cout << "Windows private-key ACL checks passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        remove_file();
        remove_certificate();
        std::cerr << "Windows private-key ACL test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
