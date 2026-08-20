#include "personal_vpn/control_messages.hpp"
#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/protocol.hpp"
#include "personal_vpn/tls_security.hpp"
#include "personal_vpn/tls_tunnel_session.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>

#include <array>
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
using namespace personal_vpn::core;
using namespace personal_vpn::protocol;
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

ssl::context make_client_context(const std::string& ca_file,
                                 const std::string& certificate_file,
                                 const std::string& key_file)
{
    ssl::context context(ssl::context::tls_client);
    context.load_verify_file(ca_file);
    context.use_certificate_chain_file(certificate_file);
    context.use_private_key_file(key_file, ssl::context::pem);
    context.set_verify_mode(ssl::verify_peer);
    context.set_verify_callback(ssl::host_name_verification("localhost"));
    allow_test_clock_skew(context);
    return context;
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
        throw std::runtime_error("server declared an oversized test frame");
    }
    const auto header_size = bytes.size();
    bytes.resize(header_size + payload_size);
    if (payload_size != 0U)
    {
        boost::asio::read(
            stream,
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

std::vector<std::uint8_t> ipv4_packet(const Ipv4Address source,
                                      const Ipv4Address destination)
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

void run_end_to_end_session(const std::string& ca_file,
                            const std::string& server_certificate,
                            const std::string& server_key,
                            const std::string& client_certificate,
                            const std::string& client_key)
{
    auto server_context = make_server_tls_context(
        ServerTlsConfig{server_certificate, server_key, ca_file});
    allow_test_clock_skew(server_context);
    auto client_context = make_client_context(ca_file, client_certificate, client_key);

    boost::asio::io_context server_io;
    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0U));
    LeaseManager leases(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.10"),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        60s);

    std::promise<std::shared_ptr<TlsTunnelSession>> session_promise;
    auto session_future = session_promise.get_future();
    std::promise<std::vector<std::uint8_t>> packet_promise;
    auto packet_future = packet_promise.get_future();
    std::promise<void> closed_promise;
    auto closed_future = closed_promise.get_future();
    std::exception_ptr server_exception;

    std::thread server_thread(
        [&]
        {
            try
            {
                tcp::socket socket(server_io);
                acceptor.accept(socket);
                auto session = std::make_shared<TlsTunnelSession>(
                    TlsTunnelSession::TlsStream(std::move(socket), server_context),
                    leases,
                    [&packet_promise](const std::vector<std::uint8_t>& packet)
                    { packet_promise.set_value(packet); },
                    [&closed_promise] { closed_promise.set_value(); });
                session_promise.set_value(session);
                session->start();
                server_io.run();
            }
            catch (...)
            {
                server_exception = std::current_exception();
            }
        });

    boost::asio::io_context client_io;
    ssl::stream<tcp::socket> client(client_io, client_context);
    if (SSL_set_tlsext_host_name(client.native_handle(), "localhost") != 1)
    {
        throw std::runtime_error("failed to configure client SNI");
    }
    client.lowest_layer().connect(acceptor.local_endpoint());
    client.handshake(ssl::stream_base::client);
    const auto session = session_future.get();

    const Frame hello{MessageType::ClientHello,
                      0U,
                      1U,
                      encode_client_hello(ClientHello{1'280U, kCapabilityIpv4})};
    const auto hello_bytes = encode_frame(hello);
    constexpr std::size_t fragment_size = 7U;
    boost::asio::write(client, boost::asio::buffer(hello_bytes.data(), fragment_size));
    boost::asio::write(client,
                       boost::asio::buffer(hello_bytes.data() + fragment_size,
                                           hello_bytes.size() - fragment_size));

    const auto assignment_frame = read_frame(client);
    check(assignment_frame.type == MessageType::IpAssign && assignment_frame.sequence == 1U,
          "mTLS HELLO produces the first IP_ASSIGN frame");
    const auto assignment = decode_ip_assignment(assignment_frame.payload);
    check(assignment.client_address == parse_ipv4_address("10.8.0.2") &&
              assignment.gateway_address == parse_ipv4_address("10.8.0.1") &&
              assignment.mtu == 1'280U,
          "server authoritatively assigns the expected address, gateway, and MTU");

    const LivenessProbe probe{0x1020304050607080ULL};
    const Frame ping{MessageType::Ping, 0U, 2U, encode_liveness_probe(probe)};
    const auto client_packet =
        ipv4_packet(assignment.client_address, parse_ipv4_address("192.0.2.10"));
    const Frame data{MessageType::DataIpv4, 0U, 3U, client_packet};
    auto coalesced = encode_frame(ping);
    const auto data_bytes = encode_frame(data);
    coalesced.insert(coalesced.end(), data_bytes.begin(), data_bytes.end());
    boost::asio::write(client, boost::asio::buffer(coalesced));

    const auto pong = read_frame(client);
    check(pong.type == MessageType::Pong && pong.sequence == 2U &&
              decode_liveness_probe(pong.payload) == probe,
          "coalesced PING and DATA frames preserve order and return PONG");
    check(packet_future.wait_for(3s) == std::future_status::ready &&
              packet_future.get() == client_packet,
          "authenticated client IPv4 packet reaches the server packet sink");

    const auto server_packet =
        ipv4_packet(parse_ipv4_address("192.0.2.10"), assignment.client_address);
    session->send_ipv4_from_tun(server_packet);
    const auto server_data = read_frame(client);
    check(server_data.type == MessageType::DataIpv4 && server_data.sequence == 3U &&
              server_data.payload == server_packet,
          "server packet for the lease is delivered to the authenticated client");

    const Frame close{MessageType::Close,
                      0U,
                      4U,
                      encode_close_message(CloseMessage{0U})};
    const auto close_bytes = encode_frame(close);
    boost::asio::write(client, boost::asio::buffer(close_bytes));
    check(closed_future.wait_for(3s) == std::future_status::ready,
          "client CLOSE terminates the transport promptly");

    boost::system::error_code ignored;
    client.lowest_layer().close(ignored);
    server_thread.join();
    if (server_exception)
    {
        std::rethrow_exception(server_exception);
    }
    check(leases.active_count() == 0U, "closed transport releases the authenticated lease");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 6)
    {
        std::cerr << "expected CA, server certificate, server key, client certificate, client key\n";
        return EXIT_FAILURE;
    }

    run_end_to_end_session(argv[1], argv[2], argv[3], argv[4], argv[5]);
    if (failures != 0)
    {
        std::cerr << failures << " TLS tunnel session test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All TLS tunnel session tests passed\n";
    return EXIT_SUCCESS;
}
