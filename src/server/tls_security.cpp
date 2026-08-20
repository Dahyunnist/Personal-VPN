#include "personal_vpn/tls_security.hpp"

#include <boost/asio/ssl.hpp>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

#if defined(__unix__)
#include <sys/stat.h>
#endif

namespace personal_vpn::server
{
namespace
{

using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;

struct FileCloser
{
    void operator()(FILE* file) const noexcept
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<FILE, FileCloser>;

std::string openssl_error(const std::string& prefix)
{
    const auto code = ERR_get_error();
    if (code == 0U)
    {
        return prefix;
    }
    std::array<char, 256U> buffer{};
    ERR_error_string_n(code, buffer.data(), buffer.size());
    return prefix + ": " + buffer.data();
}

std::string certificate_sha256(X509* certificate)
{
    if (certificate == nullptr)
    {
        throw std::runtime_error("peer did not provide an X.509 certificate");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0U;
    if (X509_digest(certificate, EVP_sha256(), digest.data(), &digest_size) != 1)
    {
        throw std::runtime_error(openssl_error("failed to calculate certificate fingerprint"));
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string identity = "sha256:";
    identity.reserve(7U + static_cast<std::size_t>(digest_size) * 2U);
    for (unsigned int index = 0U; index < digest_size; ++index)
    {
        identity.push_back(hex[(digest[index] >> 4U) & 0x0FU]);
        identity.push_back(hex[digest[index] & 0x0FU]);
    }
    return identity;
}

} // namespace

boost::asio::ssl::context make_server_tls_context(const ServerTlsConfig& config)
{
    if (config.certificate_chain_file.empty() || config.private_key_file.empty() ||
        config.client_ca_file.empty())
    {
        throw std::invalid_argument("server TLS credential paths must not be empty");
    }

    boost::asio::ssl::context context(boost::asio::ssl::context::tls_server);
    context.set_options(boost::asio::ssl::context::default_workarounds |
                        boost::asio::ssl::context::no_sslv2 |
                        boost::asio::ssl::context::no_sslv3 |
                        boost::asio::ssl::context::no_tlsv1 |
                        boost::asio::ssl::context::no_tlsv1_1);
    context.use_certificate_chain_file(config.certificate_chain_file);
    context.use_private_key_file(config.private_key_file, boost::asio::ssl::context::pem);
    context.load_verify_file(config.client_ca_file);
    context.set_verify_mode(boost::asio::ssl::verify_peer |
                            boost::asio::ssl::verify_fail_if_no_peer_cert);

    if (!config.client_crl_file.empty())
    {
        auto* store = SSL_CTX_get_cert_store(context.native_handle());
        auto* lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
        if (lookup == nullptr ||
            X509_load_crl_file(lookup, config.client_crl_file.c_str(), X509_FILETYPE_PEM) != 1)
        {
            throw std::runtime_error(openssl_error("failed to load client certificate CRL"));
        }
        if (X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL) != 1)
        {
            throw std::runtime_error(openssl_error("failed to enable client CRL checking"));
        }
    }

    auto* native = context.native_handle();
    if (SSL_CTX_check_private_key(native) != 1)
    {
        throw std::runtime_error(openssl_error("server private key does not match certificate"));
    }
    if (SSL_CTX_set_min_proto_version(native, TLS1_2_VERSION) != 1)
    {
        throw std::runtime_error(openssl_error("failed to set minimum TLS version"));
    }
    if (SSL_CTX_set_cipher_list(native,
                                "ECDHE-ECDSA-AES256-GCM-SHA384:"
                                "ECDHE-RSA-AES256-GCM-SHA384:"
                                "ECDHE-ECDSA-CHACHA20-POLY1305:"
                                "ECDHE-RSA-CHACHA20-POLY1305:"
                                "ECDHE-ECDSA-AES128-GCM-SHA256:"
                                "ECDHE-RSA-AES128-GCM-SHA256") != 1)
    {
        throw std::runtime_error(openssl_error("failed to configure TLS 1.2 cipher suites"));
    }
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
    if (SSL_CTX_set_ciphersuites(native,
                                 "TLS_AES_256_GCM_SHA384:"
                                 "TLS_CHACHA20_POLY1305_SHA256:"
                                 "TLS_AES_128_GCM_SHA256") != 1)
    {
        throw std::runtime_error(openssl_error("failed to configure TLS 1.3 cipher suites"));
    }
#endif
    return context;
}

std::string peer_certificate_sha256(SSL* ssl_handle)
{
    if (ssl_handle == nullptr)
    {
        throw std::invalid_argument("SSL handle must not be null");
    }
    if (SSL_get_verify_result(ssl_handle) != X509_V_OK)
    {
        throw std::runtime_error("peer certificate verification did not succeed");
    }
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    X509Ptr certificate(SSL_get1_peer_certificate(ssl_handle), &X509_free);
#else
    X509Ptr certificate(SSL_get_peer_certificate(ssl_handle), &X509_free);
#endif
    return certificate_sha256(certificate.get());
}

void validate_server_private_key_permissions(const std::string& private_key_file)
{
    if (private_key_file.empty())
    {
        throw std::invalid_argument("server private key path must not be empty");
    }
#if defined(__unix__)
    struct stat status{};
    if (::stat(private_key_file.c_str(), &status) != 0)
    {
        throw std::runtime_error("failed to inspect server private key permissions");
    }
    if (!S_ISREG(status.st_mode))
    {
        throw std::invalid_argument("server private key must be a regular file");
    }
    if ((status.st_mode & (S_IRWXG | S_IRWXO)) != 0)
    {
        throw std::invalid_argument(
            "server private key must not grant group or other permissions");
    }
#endif
}

std::string certificate_file_sha256(const std::string& certificate_file)
{
    FilePtr file(std::fopen(certificate_file.c_str(), "rb"));
    if (!file)
    {
        throw std::runtime_error("failed to open certificate file: " + certificate_file);
    }
    X509Ptr certificate(PEM_read_X509(file.get(), nullptr, nullptr, nullptr), &X509_free);
    if (!certificate)
    {
        throw std::runtime_error(openssl_error("failed to parse certificate file"));
    }
    return certificate_sha256(certificate.get());
}

} // namespace personal_vpn::server
