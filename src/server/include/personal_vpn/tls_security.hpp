#ifndef PERSONAL_VPN_TLS_SECURITY_HPP
#define PERSONAL_VPN_TLS_SECURITY_HPP

#include <boost/asio/ssl/context.hpp>

#include <openssl/ssl.h>

#include <string>

namespace personal_vpn::server
{

struct ServerTlsConfig
{
    std::string certificate_chain_file;
    std::string private_key_file;
    std::string client_ca_file;
};

[[nodiscard]] boost::asio::ssl::context make_server_tls_context(const ServerTlsConfig& config);

[[nodiscard]] std::string peer_certificate_sha256(SSL* ssl_handle);
[[nodiscard]] std::string certificate_file_sha256(const std::string& certificate_file);

} // namespace personal_vpn::server

#endif
