#ifndef PERSONAL_VPN_CLIENT_TLS_SECURITY_HPP
#define PERSONAL_VPN_CLIENT_TLS_SECURITY_HPP

#include <boost/asio/ssl/context.hpp>

#include <openssl/ssl.h>

#include <string>

namespace personal_vpn::client
{

struct ClientTlsConfig
{
    std::string ca_file;
    std::string certificate_chain_file;
    std::string private_key_file;
    std::string expected_server_name;
};

[[nodiscard]] boost::asio::ssl::context make_client_tls_context(
    const ClientTlsConfig& config);

void configure_client_sni(SSL* ssl_handle, const std::string& expected_server_name);

} // namespace personal_vpn::client

#endif
