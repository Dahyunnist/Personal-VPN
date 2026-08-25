#include "personal_vpn/client_tls_security.hpp"
#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/linux_tun_device.hpp"
#include "personal_vpn/tls_security.hpp"
#include "personal_vpn/tls_tunnel_client.hpp"
#include "personal_vpn/tls_tunnel_server.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using namespace std::chrono_literals;
using personal_vpn::client::ClientTlsConfig;
using personal_vpn::client::TlsTunnelClient;
using personal_vpn::client::make_client_tls_context;
using personal_vpn::core::Ipv4Address;
using personal_vpn::core::LeaseManager;
using personal_vpn::core::parse_ipv4_address;
using personal_vpn::protocol::IpAssignment;
using personal_vpn::server::LinuxTunDevice;
using personal_vpn::server::ServerTlsConfig;
using personal_vpn::server::TlsTunnelServer;
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

std::vector<std::uint8_t> ipv4_packet(const Ipv4Address& source,
                                      const Ipv4Address& destination)
{
    std::vector<std::uint8_t> packet(20U, 0U);
    packet[0] = 0x45U;
    packet[3] = 20U;
    packet[8] = 64U;
    packet[9] = 1U;
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        packet[12U + index] = source[index];
        packet[16U + index] = destination[index];
    }
    return packet;
}

void run_client_server_round_trip(const std::vector<std::string>& paths)
{
    std::array<int, 2U> descriptors{};
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, descriptors.data()) != 0)
    {
        throw std::runtime_error("failed to create test TUN descriptor pair");
    }
    timeval timeout{};
    timeout.tv_sec = 3;
    if (::setsockopt(descriptors[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        throw std::runtime_error("failed to set test TUN timeout");
    }

    boost::asio::io_context server_io;
    auto tun_device = LinuxTunDevice::adopt(server_io, descriptors[0], "client-test-tun");
    auto server_context = make_server_tls_context(ServerTlsConfig{paths[1], paths[2], paths[0]});
    allow_test_clock_skew(server_context);
    LeaseManager leases(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.20"),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        60s);
    auto server = std::make_shared<TlsTunnelServer>(server_io,
                                                    tcp::endpoint(tcp::v4(), 0U),
                                                    server_context,
                                                    leases,
                                                    tun_device,
                                                    4U);
    server->start();
    std::thread server_thread([&server_io] { server_io.run(); });

    boost::asio::io_context client_io;
    auto client_context = make_client_tls_context(
        ClientTlsConfig{paths[0], paths[3], paths[4], "localhost"});
    allow_test_clock_skew(client_context);
    TlsTunnelClient::TlsStream stream(client_io, client_context);
    stream.lowest_layer().connect(server->local_endpoint());

    std::promise<IpAssignment> assignment_promise;
    auto assignment_future = assignment_promise.get_future();
    std::promise<std::vector<std::uint8_t>> downlink_promise;
    auto downlink_future = downlink_promise.get_future();
    std::promise<void> close_promise;
    auto close_future = close_promise.get_future();
    std::atomic<bool> assignment_delivered{false};
    std::atomic<bool> packet_delivered{false};

    auto client = std::make_shared<TlsTunnelClient>(
        std::move(stream),
        "localhost",
        [&assignment_promise, &assignment_delivered](const IpAssignment& assignment)
        {
            if (!assignment_delivered.exchange(true))
            {
                assignment_promise.set_value(assignment);
            }
            return true;
        },
        [&downlink_promise, &packet_delivered](const std::vector<std::uint8_t>& packet)
        {
            if (!packet_delivered.exchange(true))
            {
                downlink_promise.set_value(packet);
            }
        },
        [&close_promise](const auto&) { close_promise.set_value(); },
        1'280U);
    client->start();
    std::thread client_thread([&client_io] { client_io.run(); });

    check(assignment_future.wait_for(5s) == std::future_status::ready,
          "portable client completes mTLS and receives a server assignment");
    if (assignment_future.wait_for(0s) != std::future_status::ready)
    {
        client->stop();
        client_thread.join();
        server->stop();
        server_thread.join();
        ::close(descriptors[1]);
        return;
    }
    const auto assignment = assignment_future.get();
    check(assignment.client_address == parse_ipv4_address("10.8.0.2") &&
              assignment.gateway_address == parse_ipv4_address("10.8.0.1") &&
              assignment.mtu == 1'280U,
          "client accepts the authoritative lease and negotiated MTU");

    const auto uplink =
        ipv4_packet(assignment.client_address, parse_ipv4_address("198.51.100.7"));
    client->send_ipv4_from_tun(uplink);
    std::array<std::uint8_t, 64U> tun_buffer{};
    const auto received = ::recv(descriptors[1], tun_buffer.data(), tun_buffer.size(), 0);
    check(received == static_cast<ssize_t>(uplink.size()) &&
              std::equal(uplink.begin(), uplink.end(), tun_buffer.begin()),
          "client uplink crosses framed TLS and reaches the server TUN intact");

    const auto downlink =
        ipv4_packet(parse_ipv4_address("192.0.2.9"), assignment.client_address);
    check(::send(descriptors[1], downlink.data(), downlink.size(), MSG_NOSIGNAL) ==
              static_cast<ssize_t>(downlink.size()),
          "test downlink enters the server TUN");
    check(downlink_future.wait_for(5s) == std::future_status::ready,
          "client receives a framed TLS downlink");
    if (downlink_future.wait_for(0s) == std::future_status::ready)
    {
        check(downlink_future.get() == downlink,
              "client delivers exactly one complete IPv4 packet to its packet sink");
    }

    client->ping(0x1020304050607080ULL);
    client->stop();
    check(close_future.wait_for(5s) == std::future_status::ready,
          "client performs an orderly protocol close after queued writes");
    client_io.stop();
    client_thread.join();
    server->stop();
    server_thread.join();
    check(server->active_session_count() == 0U && leases.active_count() == 0U,
          "client shutdown releases server route and lease state");
    ::close(descriptors[1]);
}

void reject_wrong_server_name(const std::vector<std::string>& paths)
{
    std::array<int, 2U> descriptors{};
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, descriptors.data()) != 0)
    {
        throw std::runtime_error("failed to create hostname-test TUN descriptor pair");
    }
    boost::asio::io_context server_io;
    auto tun_device = LinuxTunDevice::adopt(server_io, descriptors[0], "hostname-test-tun");
    auto server_context = make_server_tls_context(ServerTlsConfig{paths[1], paths[2], paths[0]});
    allow_test_clock_skew(server_context);
    LeaseManager leases(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.3"),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        60s);
    auto server = std::make_shared<TlsTunnelServer>(server_io,
                                                    tcp::endpoint(tcp::v4(), 0U),
                                                    server_context,
                                                    leases,
                                                    tun_device,
                                                    2U);
    server->start();
    std::thread server_thread([&server_io] { server_io.run(); });

    boost::asio::io_context client_io;
    auto client_context = make_client_tls_context(
        ClientTlsConfig{paths[0], paths[3], paths[4], "wrong.example.invalid"});
    allow_test_clock_skew(client_context);
    TlsTunnelClient::TlsStream stream(client_io, client_context);
    stream.lowest_layer().connect(server->local_endpoint());
    std::promise<void> close_promise;
    auto close_future = close_promise.get_future();
    std::atomic<bool> assigned{false};
    auto client = std::make_shared<TlsTunnelClient>(
        std::move(stream),
        "wrong.example.invalid",
        [&assigned](const IpAssignment&)
        {
            assigned = true;
            return true;
        },
        TlsTunnelClient::PacketSink{},
        [&close_promise](const auto&) { close_promise.set_value(); });
    client->start();
    std::thread client_thread([&client_io] { client_io.run(); });
    check(close_future.wait_for(5s) == std::future_status::ready && !assigned.load(),
          "hostname mismatch fails closed before protocol establishment");
    client_io.stop();
    client_thread.join();
    server->stop();
    server_thread.join();
    ::close(descriptors[1]);
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
    run_client_server_round_trip(paths);
    reject_wrong_server_name(paths);
    if (failures != 0)
    {
        std::cerr << failures << " TLS tunnel client test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All TLS tunnel client tests passed\n";
    return EXIT_SUCCESS;
}
