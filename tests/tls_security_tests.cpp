#include "personal_vpn/tls_security.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace
{
using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using personal_vpn::server::ServerTlsConfig;
using personal_vpn::server::certificate_file_sha256;
using personal_vpn::server::make_server_tls_context;
using personal_vpn::server::peer_certificate_sha256;

constexpr std::time_t certificate_clock_skew_tolerance_seconds = 300;

void allow_test_clock_skew(ssl::context& context)
{
    X509_VERIFY_PARAM_set_time(
        SSL_CTX_get0_param(context.native_handle()),
        std::time(nullptr) + certificate_clock_skew_tolerance_seconds);
}

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

ssl::context make_client_context(const std::string& ca_file,
                                 const std::string& certificate_file,
                                 const std::string& key_file,
                                 const std::string& expected_host)
{
    ssl::context context(ssl::context::tls_client);
    context.load_verify_file(ca_file);
    if (!certificate_file.empty())
    {
        context.use_certificate_chain_file(certificate_file);
        context.use_private_key_file(key_file, ssl::context::pem);
    }
    context.set_verify_mode(ssl::verify_peer);
    context.set_verify_callback(ssl::host_name_verification(expected_host));
    allow_test_clock_skew(context);
    if (SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION) != 1)
    {
        throw std::runtime_error("failed to set client minimum TLS version");
    }
    return context;
}

struct HandshakeResult
{
    boost::system::error_code server_error;
    boost::system::error_code client_error;
    std::string server_peer_identity;
    int negotiated_version{0};
};

HandshakeResult handshake_pair(ssl::context& server_context,
                               ssl::context& client_context,
                               const bool send_sni)
{
    boost::asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0U));
    const auto endpoint = acceptor.local_endpoint();
    HandshakeResult result;

    std::thread server_thread([&]
                              {
                                  tcp::socket socket(server_io);
                                  acceptor.accept(socket, result.server_error);
                                  if (result.server_error)
                                  {
                                      return;
                                  }
                                  ssl::stream<tcp::socket> stream(std::move(socket), server_context);
                                  stream.handshake(ssl::stream_base::server, result.server_error);
                                  if (!result.server_error)
                                  {
                                      result.server_peer_identity =
                                          peer_certificate_sha256(stream.native_handle());
                                      result.negotiated_version = SSL_version(stream.native_handle());
                                  }
                              });

    boost::asio::io_context client_io;
    ssl::stream<tcp::socket> client(client_io, client_context);
    if (send_sni &&
        SSL_set_tlsext_host_name(client.native_handle(), "localhost") != 1)
    {
        throw std::runtime_error("failed to configure client SNI");
    }
    client.lowest_layer().connect(endpoint, result.client_error);
    if (!result.client_error)
    {
        client.handshake(ssl::stream_base::client, result.client_error);
    }
    boost::system::error_code ignored;
    client.lowest_layer().close(ignored);
    server_thread.join();
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 6)
    {
        std::cerr << "expected CA, server certificate, server key, client certificate, client key\n";
        return EXIT_FAILURE;
    }
    const std::string ca_file = argv[1];
    const std::string server_certificate = argv[2];
    const std::string server_key = argv[3];
    const std::string client_certificate = argv[4];
    const std::string client_key = argv[5];

    auto server_context = make_server_tls_context(
        ServerTlsConfig{server_certificate, server_key, ca_file});
    // WSL and virtualized CI clocks can move backwards by a few seconds while the
    // short-lived test PKI is generated. Production contexts continue to use the
    // system time without this test-only tolerance.
    allow_test_clock_skew(server_context);

    auto valid_client =
        make_client_context(ca_file, client_certificate, client_key, "localhost");
    const auto success = handshake_pair(server_context, valid_client, true);
    check(!success.server_error && !success.client_error, "valid mutual TLS handshake succeeds");
    check(success.server_peer_identity == certificate_file_sha256(client_certificate),
          "server identity is the verified client certificate fingerprint");
    check(success.negotiated_version >= TLS1_2_VERSION, "negotiated TLS version is at least 1.2");

    auto anonymous_client = make_client_context(ca_file, "", "", "localhost");
    const auto no_certificate = handshake_pair(server_context, anonymous_client, true);
    check(static_cast<bool>(no_certificate.server_error),
          "server rejects a client without a certificate");

    auto wrong_host_client =
        make_client_context(ca_file, client_certificate, client_key, "not-localhost.example");
    const auto wrong_host = handshake_pair(server_context, wrong_host_client, true);
    check(static_cast<bool>(wrong_host.client_error),
          "client rejects a server certificate for the wrong host");

    if (failures != 0)
    {
        std::cerr << failures << " TLS security test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All TLS security tests passed\n";
    return EXIT_SUCCESS;
}
