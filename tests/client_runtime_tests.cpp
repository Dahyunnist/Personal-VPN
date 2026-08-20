#include "personal_vpn/client_runtime.hpp"
#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/linux_tun_device.hpp"
#include "personal_vpn/tls_security.hpp"
#include "personal_vpn/tls_tunnel_server.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <openssl/ssl.h>

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using namespace std::chrono_literals;
using namespace personal_vpn::client;
using personal_vpn::core::Ipv4Address;
using personal_vpn::core::LeaseManager;
using personal_vpn::core::parse_ipv4_address;
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

class FakePacketDevice final : public ClientPacketDevice
{
   public:
    void open_adapter() override { events.push_back("open"); }
    void start_packet_session() override
    {
        events.push_back("start-session");
        std::lock_guard<std::mutex> lock(mutex);
        interrupted = false;
    }
    bool add_interface_address(const InterfaceAddress& value) override
    {
        events.push_back("add-address");
        address = value;
        return true;
    }
    std::optional<std::uint32_t> set_interface_mtu(const std::uint32_t value) override
    {
        events.push_back("set-mtu-" + std::to_string(value));
        return 1'500U;
    }
    bool add_route(const Ipv4Route& value) override
    {
        events.push_back("add-route-" + std::to_string(value.prefix_length));
        return true;
    }
    void remove_route(const Ipv4Route& value) noexcept override
    {
        events.push_back("remove-route-" + std::to_string(value.prefix_length));
    }
    void restore_interface_mtu(const std::uint32_t value) noexcept override
    {
        events.push_back("restore-mtu-" + std::to_string(value));
    }
    void remove_interface_address(const InterfaceAddress&) noexcept override
    {
        events.push_back("remove-address");
    }
    void stop_packet_session() noexcept override { events.push_back("stop-session"); }
    void close_adapter() noexcept override { events.push_back("close"); }

    std::optional<std::vector<std::uint8_t>> receive_packet() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this] { return interrupted || !uplinks.empty(); });
        if (interrupted)
        {
            return std::nullopt;
        }
        auto packet = std::move(uplinks.front());
        uplinks.pop_front();
        return packet;
    }

    void interrupt_receive() noexcept override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            interrupted = true;
        }
        condition.notify_all();
    }

    void send_packet(const std::vector<std::uint8_t>& packet) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            downlinks.push_back(packet);
        }
        condition.notify_all();
    }

    void push_uplink(std::vector<std::uint8_t> packet)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            uplinks.push_back(std::move(packet));
        }
        condition.notify_all();
    }

    bool wait_for_downlink(const std::vector<std::uint8_t>& expected)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!condition.wait_for(lock, 5s, [this] { return !downlinks.empty(); }))
        {
            return false;
        }
        return downlinks.front() == expected;
    }

    std::vector<std::string> events;
    InterfaceAddress address{};
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::vector<std::uint8_t>> uplinks;
    std::deque<std::vector<std::uint8_t>> downlinks;
    bool interrupted{false};
};

void run_runtime_round_trip(const std::vector<std::string>& paths)
{
    std::array<int, 2U> descriptors{};
    if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, descriptors.data()) != 0)
    {
        throw std::runtime_error("failed to create runtime test TUN pair");
    }
    timeval timeout{};
    timeout.tv_sec = 3;
    static_cast<void>(::setsockopt(descriptors[1], SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                  sizeof(timeout)));

    boost::asio::io_context server_io;
    auto tun_device = LinuxTunDevice::adopt(server_io, descriptors[0], "runtime-test-tun");
    auto server_context = make_server_tls_context(ServerTlsConfig{paths[1], paths[2], paths[0]});
    allow_test_clock_skew(server_context);
    LeaseManager leases(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.10"),
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

    ClientConfig config;
    config.server_host = "127.0.0.1";
    config.server_port = server->local_endpoint().port();
    config.expected_server_name = "localhost";
    config.ca_file = paths[0];
    config.certificate_chain_file = paths[3];
    config.private_key_file = paths[4];
    config.requested_mtu = 1'280U;
    config.routes = {"10.20.0.0/16"};

    auto device = std::make_unique<FakePacketDevice>();
    auto* device_observer = device.get();
    std::promise<void> connected_promise;
    auto connected_future = connected_promise.get_future();
    std::atomic<bool> connected_signaled{false};
    ClientRuntime runtime(
        std::move(config),
        std::move(device),
        [&connected_promise, &connected_signaled](const ClientRuntimeEvent& event)
        {
            if (event.state == ClientRuntimeState::Connected &&
                !connected_signaled.exchange(true))
            {
                connected_promise.set_value();
            }
        },
        5s);
    runtime.start();
    check(connected_future.wait_for(5s) == std::future_status::ready,
          "runtime reaches Connected only after TLS assignment and network transaction");
    if (connected_future.wait_for(0s) != std::future_status::ready)
    {
        runtime.stop();
        runtime.wait();
        server->stop();
        server_thread.join();
        ::close(descriptors[1]);
        return;
    }
    check(device_observer->address.address == parse_ipv4_address("10.8.0.2") &&
              device_observer->address.prefix_length == 24U,
          "runtime configures the packet device from the authenticated assignment");

    const auto uplink = ipv4_packet(parse_ipv4_address("10.8.0.2"),
                                    parse_ipv4_address("198.51.100.11"));
    device_observer->push_uplink(uplink);
    std::array<std::uint8_t, 64U> tun_buffer{};
    const auto received = ::recv(descriptors[1], tun_buffer.data(), tun_buffer.size(), 0);
    check(received == static_cast<ssize_t>(uplink.size()) &&
              std::equal(uplink.begin(), uplink.end(), tun_buffer.begin()),
          "blocking virtual-adapter reader forwards an intact framed uplink");

    const auto downlink = ipv4_packet(parse_ipv4_address("192.0.2.55"),
                                      parse_ipv4_address("10.8.0.2"));
    check(::send(descriptors[1], downlink.data(), downlink.size(), MSG_NOSIGNAL) ==
              static_cast<ssize_t>(downlink.size()),
          "runtime test downlink enters server TUN");
    check(device_observer->wait_for_downlink(downlink),
          "runtime copies a complete TLS downlink into the virtual packet device");

    runtime.stop();
    runtime.wait();
    check(runtime.state() == ClientRuntimeState::Stopped && runtime.failure_message().empty(),
          "intentional stop joins all runtime threads without a false failure");
    const std::vector<std::string> rollback_tail{"remove-route-16",
                                                 "restore-mtu-1500",
                                                 "remove-address",
                                                 "stop-session",
                                                 "close"};
    check(device_observer->events.size() >= rollback_tail.size() &&
              std::equal(rollback_tail.begin(),
                         rollback_tail.end(),
                         device_observer->events.end() -
                             static_cast<std::ptrdiff_t>(rollback_tail.size())),
          "runtime shutdown performs exact network rollback before returning");

    server->stop();
    server_thread.join();
    check(leases.active_count() == 0U, "runtime shutdown releases its server lease");
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
    run_runtime_round_trip(std::vector<std::string>(argv + 1, argv + argc));
    if (failures != 0)
    {
        std::cerr << failures << " client runtime test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All client runtime tests passed\n";
    return EXIT_SUCCESS;
}
