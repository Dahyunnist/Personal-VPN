#include "personal_vpn/control_messages.hpp"
#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/linux_tun_device.hpp"
#include "personal_vpn/protocol.hpp"
#include "personal_vpn/tls_security.hpp"
#include "personal_vpn/tls_tunnel_server.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
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
using namespace personal_vpn::core;
using namespace personal_vpn::protocol;
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

std::unique_ptr<ssl::context> make_client_context(const std::string& ca_file,
                                                  const std::string& certificate_file,
                                                  const std::string& key_file)
{
    auto context = std::make_unique<ssl::context>(ssl::context::tls_client);
    context->load_verify_file(ca_file);
    context->use_certificate_chain_file(certificate_file);
    context->use_private_key_file(key_file, ssl::context::pem);
    context->set_verify_mode(ssl::verify_peer);
    context->set_verify_callback(ssl::host_name_verification("localhost"));
    allow_test_clock_skew(*context);
    return context;
}

struct TestClient
{
    boost::asio::io_context io_context;
    std::unique_ptr<ssl::context> context;
    ssl::stream<tcp::socket> stream;

    TestClient(const std::string& ca_file,
               const std::string& certificate_file,
               const std::string& key_file)
        : context(make_client_context(ca_file, certificate_file, key_file)),
          stream(io_context, *context)
    {
    }
};

void connect_client(TestClient& client, const tcp::endpoint& endpoint)
{
    if (SSL_set_tlsext_host_name(client.stream.native_handle(), "localhost") != 1)
    {
        throw std::runtime_error("failed to configure client SNI");
    }
    client.stream.lowest_layer().connect(endpoint);
    client.stream.handshake(ssl::stream_base::client);
}

Frame read_frame(ssl::stream<tcp::socket>& stream)
{
    std::vector<std::uint8_t> bytes(kHeaderSize);
    boost::asio::read(stream, boost::asio::buffer(bytes));
    const auto payload_size =
        (static_cast<std::uint32_t>(bytes[8]) << 24U) |
        (static_cast<std::uint32_t>(bytes[9]) << 16U) |
        (static_cast<std::uint32_t>(bytes[10]) << 8U) |
        static_cast<std::uint32_t>(bytes[11]);
    if (payload_size > kMaxPayloadSize)
    {
        throw std::runtime_error("server declared an oversized frame");
    }
    const auto header_size = bytes.size();
    bytes.resize(header_size + payload_size);
    if (payload_size != 0U)
    {
        boost::asio::read(stream,
                          boost::asio::buffer(bytes.data() + header_size, payload_size));
    }
    FrameDecoder decoder;
    const auto frames = decoder.push(bytes);
    if (frames.size() != 1U)
    {
        throw std::runtime_error("expected exactly one server frame");
    }
    return frames.front();
}

IpAssignment establish(TestClient& client)
{
    const Frame hello{MessageType::ClientHello,
                      0U,
                      1U,
                      encode_client_hello(ClientHello{1'280U, kCapabilityIpv4})};
    const auto bytes = encode_frame(hello);
    boost::asio::write(client.stream, boost::asio::buffer(bytes));
    const auto response = read_frame(client.stream);
    if (response.type != MessageType::IpAssign)
    {
        throw std::runtime_error("server did not assign a client address");
    }
    return decode_ip_assignment(response.payload);
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

void send_close(TestClient& client, const std::uint64_t sequence)
{
    const auto bytes = encode_frame(
        Frame{MessageType::Close, 0U, sequence, encode_close_message(CloseMessage{0U})});
    boost::asio::write(client.stream, boost::asio::buffer(bytes));
}

void run_multi_client_server(const std::vector<std::string>& paths)
{
    std::array<int, 2U> descriptors{};
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, descriptors.data()) != 0)
    {
        throw std::runtime_error("failed to create test TUN descriptor pair");
    }
    timeval timeout{};
    timeout.tv_sec = 2;
    if (::setsockopt(descriptors[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        throw std::runtime_error("failed to set test TUN timeout");
    }

    boost::asio::io_context server_io;
    auto tun_device = LinuxTunDevice::adopt(server_io, descriptors[0], "test-tun");
    auto server_context = make_server_tls_context(ServerTlsConfig{paths[1], paths[2], paths[0]});
    allow_test_clock_skew(server_context);
    LeaseManager leases(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.10"),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        60s);
    std::atomic<int> shutdown_notifications{0};
    auto server = std::make_shared<TlsTunnelServer>(server_io,
                                                    tcp::endpoint(tcp::v4(), 0U),
                                                    server_context,
                                                    leases,
                                                    tun_device,
                                                    3U,
                                                    [&shutdown_notifications]
                                                    { ++shutdown_notifications; });
    server->start();
    std::vector<std::thread> server_threads;
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        server_threads.emplace_back([&server_io] { server_io.run(); });
    }

    TestClient first(paths[0], paths[3], paths[4]);
    TestClient second(paths[0], paths[5], paths[6]);
    TestClient duplicate(paths[0], paths[3], paths[4]);
    connect_client(first, server->local_endpoint());
    connect_client(second, server->local_endpoint());
    const auto first_assignment = establish(first);
    const auto second_assignment = establish(second);
    check(first_assignment.client_address == parse_ipv4_address("10.8.0.2") &&
              second_assignment.client_address == parse_ipv4_address("10.8.0.3"),
          "two certificate identities receive distinct server leases");
    check(server->active_session_count() == 2U && server->active_route_count() == 2U,
          "listener tracks two live sessions and routes");

    connect_client(duplicate, server->local_endpoint());
    TestClient over_capacity(paths[0], paths[5], paths[6]);
    bool capacity_rejected = false;
    try
    {
        connect_client(over_capacity, server->local_endpoint());
    }
    catch (const boost::system::system_error&)
    {
        capacity_rejected = true;
    }
    check(capacity_rejected && server->active_session_count() == 3U,
          "listener rejects a connection above its configured session limit");
    const Frame duplicate_hello{
        MessageType::ClientHello,
        0U,
        1U,
        encode_client_hello(ClientHello{1'280U, kCapabilityIpv4})};
    const auto duplicate_hello_bytes = encode_frame(duplicate_hello);
    boost::asio::write(duplicate.stream, boost::asio::buffer(duplicate_hello_bytes));
    const auto duplicate_response = read_frame(duplicate.stream);
    check(duplicate_response.type == MessageType::Error &&
              decode_error_message(duplicate_response.payload).code ==
                  static_cast<std::uint16_t>(SessionErrorCode::LeaseUnavailable),
          "concurrent connection with the same certificate is rejected");
    for (std::size_t attempt = 0U;
         attempt < 200U && server->active_session_count() != 2U;
         ++attempt)
    {
        std::this_thread::sleep_for(10ms);
    }
    check(server->active_session_count() == 2U && server->active_route_count() == 2U &&
              leases.active_count() == 2U,
          "rejected duplicate cannot disturb existing sessions or leases");
    boost::system::error_code duplicate_close_error;
    duplicate.stream.lowest_layer().close(duplicate_close_error);

    const auto first_downlink =
        ipv4_packet(parse_ipv4_address("192.0.2.20"), first_assignment.client_address);
    const auto second_downlink =
        ipv4_packet(parse_ipv4_address("192.0.2.21"), second_assignment.client_address);
    check(::send(descriptors[1], first_downlink.data(), first_downlink.size(), MSG_NOSIGNAL) ==
              static_cast<ssize_t>(first_downlink.size()),
          "first kernel packet enters test TUN");
    check(::send(descriptors[1], second_downlink.data(), second_downlink.size(), MSG_NOSIGNAL) ==
              static_cast<ssize_t>(second_downlink.size()),
          "second kernel packet enters test TUN");
    const auto first_frame = read_frame(first.stream);
    const auto second_frame = read_frame(second.stream);
    check(first_frame.type == MessageType::DataIpv4 && first_frame.payload == first_downlink,
          "first lease receives only its TUN downlink packet");
    check(second_frame.type == MessageType::DataIpv4 && second_frame.payload == second_downlink,
          "second lease receives only its TUN downlink packet");

    const auto uplink =
        ipv4_packet(first_assignment.client_address, parse_ipv4_address("198.51.100.8"));
    const auto uplink_frame = encode_frame(Frame{MessageType::DataIpv4, 0U, 2U, uplink});
    boost::asio::write(first.stream, boost::asio::buffer(uplink_frame));
    std::array<std::uint8_t, 64U> tun_buffer{};
    const auto received = ::recv(descriptors[1], tun_buffer.data(), tun_buffer.size(), 0);
    check(received == static_cast<ssize_t>(uplink.size()) &&
              std::equal(uplink.begin(), uplink.end(), tun_buffer.begin()),
          "authenticated client uplink is written as one complete TUN packet");

    send_close(first, 3U);
    send_close(second, 2U);
    for (std::size_t attempt = 0U;
         attempt < 200U && server->active_session_count() != 0U;
         ++attempt)
    {
        std::this_thread::sleep_for(10ms);
    }
    check(server->active_session_count() == 0U && server->active_route_count() == 0U,
          "closing both clients removes sessions and routes");

    boost::system::error_code ignored;
    first.stream.lowest_layer().close(ignored);
    second.stream.lowest_layer().close(ignored);
    server->stop();
    for (auto& server_thread : server_threads)
    {
        server_thread.join();
    }
    check(shutdown_notifications.load() == 1,
          "runtime publishes exactly one graceful shutdown notification");
    ::close(descriptors[1]);
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 8)
    {
        std::cerr << "expected CA, server pair, and two client certificate pairs\n";
        return EXIT_FAILURE;
    }
    run_multi_client_server(std::vector<std::string>(argv + 1, argv + argc));
    if (failures != 0)
    {
        std::cerr << failures << " TLS tunnel server test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All TLS tunnel server tests passed\n";
    return EXIT_SUCCESS;
}
