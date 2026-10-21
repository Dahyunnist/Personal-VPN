#include "personal_vpn/client_tls_security.hpp"
#include "personal_vpn/credential_security.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/pem.h>

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace personal_vpn::client
{
namespace
{

constexpr std::size_t kMaximumPrivateKeyBytes = 1024U * 1024U;

struct SensitiveBuffer
{
    explicit SensitiveBuffer(const std::size_t size) : bytes(size) {}
    SensitiveBuffer(const SensitiveBuffer&) = delete;
    SensitiveBuffer& operator=(const SensitiveBuffer&) = delete;

    std::vector<unsigned char> bytes;

    ~SensitiveBuffer()
    {
        if (!bytes.empty())
        {
            OPENSSL_cleanse(bytes.data(), bytes.size());
        }
    }
};

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

} // namespace

boost::asio::ssl::context make_client_tls_context(const ClientTlsConfig& config)
{
    if (config.ca_file.empty() || config.certificate_chain_file.empty() ||
        config.private_key_file.empty() || config.expected_server_name.empty())
    {
        throw std::invalid_argument("client TLS credential paths and server name must not be empty");
    }

    boost::asio::ssl::context context(boost::asio::ssl::context::tls_client);
    context.set_options(boost::asio::ssl::context::default_workarounds |
                        boost::asio::ssl::context::no_sslv2 |
                        boost::asio::ssl::context::no_sslv3 |
                        boost::asio::ssl::context::no_tlsv1 |
                        boost::asio::ssl::context::no_tlsv1_1);
    context.load_verify_file(config.ca_file);
    context.use_certificate_chain_file(config.certificate_chain_file);
    auto private_key_file = open_validated_private_key_file(config.private_key_file);
    SensitiveBuffer private_key_bytes(kMaximumPrivateKeyBytes + 1U);
    std::size_t private_key_size = 0U;
    while (private_key_size < private_key_bytes.bytes.size())
    {
        const auto bytes_read = std::fread(private_key_bytes.bytes.data() + private_key_size,
                                           1U,
                                           private_key_bytes.bytes.size() - private_key_size,
                                           private_key_file.get());
        private_key_size += bytes_read;
        if (bytes_read == 0U)
        {
            if (std::ferror(private_key_file.get()) != 0)
            {
                throw std::runtime_error("failed to read client private key");
            }
            break;
        }
    }
    if (private_key_size == 0U || private_key_size > kMaximumPrivateKeyBytes)
    {
        throw std::invalid_argument("client private key must be between 1 byte and 1 MiB");
    }
    private_key_bytes.bytes.resize(private_key_size);
    std::unique_ptr<BIO, decltype(&BIO_free)> private_key_bio(
        BIO_new_mem_buf(private_key_bytes.bytes.data(), static_cast<int>(private_key_size)),
        &BIO_free);
    if (!private_key_bio)
    {
        throw std::runtime_error(openssl_error("failed to create private-key memory BIO"));
    }
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(
        PEM_read_bio_PrivateKey(private_key_bio.get(), nullptr, nullptr, nullptr),
        &EVP_PKEY_free);
    if (!private_key)
    {
        throw std::runtime_error(openssl_error("failed to parse client private key"));
    }
    if (SSL_CTX_use_PrivateKey(context.native_handle(), private_key.get()) != 1)
    {
        throw std::runtime_error(openssl_error("failed to load client private key"));
    }
    context.set_verify_mode(boost::asio::ssl::verify_peer);
    context.set_verify_callback(
        boost::asio::ssl::host_name_verification(config.expected_server_name));

    auto* native = context.native_handle();
    if (SSL_CTX_check_private_key(native) != 1)
    {
        throw std::runtime_error(openssl_error("client private key does not match certificate"));
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

boost::asio::ssl::context make_client_tls_context(const ClientConfig& config)
{
    return make_client_tls_context(ClientTlsConfig{config.ca_file.string(),
                                                   config.certificate_chain_file.string(),
                                                   config.private_key_file.string(),
                                                   config.expected_server_name});
}

void configure_client_sni(SSL* ssl_handle, const std::string& expected_server_name)
{
    if (ssl_handle == nullptr || expected_server_name.empty())
    {
        throw std::invalid_argument("SSL handle and expected server name must be provided");
    }
    boost::system::error_code address_error;
    static_cast<void>(boost::asio::ip::make_address(expected_server_name, address_error));
    if (!address_error)
    {
        return;
    }
    if (SSL_set_tlsext_host_name(ssl_handle, expected_server_name.c_str()) != 1)
    {
        throw std::runtime_error(openssl_error("failed to configure TLS server name indication"));
    }
}

} // namespace personal_vpn::client
