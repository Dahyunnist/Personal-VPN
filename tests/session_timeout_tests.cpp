#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/server_metrics.hpp"
#include "personal_vpn/tls_security.hpp"
#include "personal_vpn/tls_tunnel_session.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using namespace std::chrono_literals;
using personal_vpn::core::Ipv4Address;
using personal_vpn::core::LeaseManager;
using personal_vpn::core::TunnelPeer;
using personal_vpn::core::parse_ipv4_address;
using personal_vpn::server::ServerMetrics;
using personal_vpn::server::ServerTlsConfig;
using personal_vpn::server::TlsTunnelSession;
using personal_vpn::server::make_server_tls_context;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void allow_test_clock_skew(ssl::context& context)
{
    constexpr std::time_t tolerance_seconds = 300;
    X509_VERIFY_PARAM_set_time(SSL_CTX_get0_param(context.native_handle()),
                               std::time(nullptr) + tolerance_seconds);
}

ssl::context client_context(const std::vector<std::string>& paths)
{
    ssl::context context(ssl::context::tls_client);
    context.load_verify_file(paths[0]);
    context.use_certificate_chain_file(paths[3]);
    context.use_private_key_file(paths[4], ssl::context::pem);
    context.set_verify_mode(ssl::verify_peer);
    context.set_verify_callback(ssl::host_name_verification("localhost"));
    allow_test_clock_skew(context);
    return context;
}

LeaseManager leases()
{
    return LeaseManager(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.3"),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        60s);
}

void handshake_deadline_releases_session(const std::vector<std::string>& paths)
{
    auto server_context =
        make_server_tls_context(ServerTlsConfig{paths[1], paths[2], paths[0]});
    allow_test_clock_skew(server_context);
    boost::asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0U));
    auto lease_manager = leases();
    ServerMetrics metrics;
    std::promise<void> closed_promise;
    auto closed_future = closed_promise.get_future();
    std::thread server_thread(
        [&]
        {
            tcp::socket socket(server_io);
            acceptor.accept(socket);
            auto session = std::make_shared<TlsTunnelSession>(
                TlsTunnelSession::TlsStream(std::move(socket), server_context),
                lease_manager,
                TlsTunnelSession::PacketSink{},
                TlsTunnelSession::EstablishedHandler{},
                [&closed_promise](const std::optional<Ipv4Address>&, const TunnelPeer*)
                { closed_promise.set_value(); },
                16U,
                64U * 1024U,
                100ms,
                1s,
                &metrics);
            session->start();
            server_io.run();
        });

    boost::asio::io_context client_io;
    tcp::socket slow_client(client_io);
    slow_client.connect(acceptor.local_endpoint());
    check(closed_future.wait_for(2s) == std::future_status::ready,
          "TCP client that never starts TLS is closed by the handshake deadline");
    boost::system::error_code ignored;
    slow_client.close(ignored);
    server_thread.join();
    const auto snapshot = metrics.snapshot();
    check(snapshot.handshake_timeouts == 1U && snapshot.closed_sessions == 1U,
          "handshake timeout and close counters are emitted exactly once");
}

void frame_idle_deadline_releases_session(const std::vector<std::string>& paths)
{
    auto server_context =
        make_server_tls_context(ServerTlsConfig{paths[1], paths[2], paths[0]});
    allow_test_clock_skew(server_context);
    auto client_tls = client_context(paths);
    boost::asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0U));
    auto lease_manager = leases();
    ServerMetrics metrics;
    std::promise<void> closed_promise;
    auto closed_future = closed_promise.get_future();
    std::thread server_thread(
        [&]
        {
            tcp::socket socket(server_io);
            acceptor.accept(socket);
            auto session = std::make_shared<TlsTunnelSession>(
                TlsTunnelSession::TlsStream(std::move(socket), server_context),
                lease_manager,
                TlsTunnelSession::PacketSink{},
                TlsTunnelSession::EstablishedHandler{},
                [&closed_promise](const std::optional<Ipv4Address>&, const TunnelPeer*)
                { closed_promise.set_value(); },
                16U,
                64U * 1024U,
                1s,
                100ms,
                &metrics);
            session->start();
            server_io.run();
        });

    boost::asio::io_context client_io;
    ssl::stream<tcp::socket> idle_client(client_io, client_tls);
    SSL_set_tlsext_host_name(idle_client.native_handle(), "localhost");
    idle_client.lowest_layer().connect(acceptor.local_endpoint());
    idle_client.handshake(ssl::stream_base::client);
    check(closed_future.wait_for(2s) == std::future_status::ready,
          "mTLS client that sends no protocol frame is closed by the idle deadline");
    boost::system::error_code ignored;
    idle_client.lowest_layer().close(ignored);
    server_thread.join();
    const auto snapshot = metrics.snapshot();
    check(snapshot.idle_timeouts == 1U && snapshot.closed_sessions == 1U,
          "idle timeout and close counters are emitted exactly once");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 6)
    {
        std::cerr << "expected CA, server certificate/key, and client certificate/key\n";
        return EXIT_FAILURE;
    }
    const std::vector<std::string> paths(argv + 1, argv + argc);
    handshake_deadline_releases_session(paths);
    frame_idle_deadline_releases_session(paths);
    if (failures != 0)
    {
        std::cerr << failures << " session timeout test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All session timeout tests passed\n";
    return EXIT_SUCCESS;
}
