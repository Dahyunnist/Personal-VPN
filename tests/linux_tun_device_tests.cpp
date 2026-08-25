#include "personal_vpn/linux_tun_device.hpp"

#include <boost/asio.hpp>

#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;
using personal_vpn::server::LinuxTunDevice;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct DescriptorPair
{
    int device{-1};
    int peer{-1};

    DescriptorPair()
    {
        std::array<int, 2U> descriptors{};
        if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, descriptors.data()) != 0)
        {
            throw std::runtime_error("failed to create test descriptor pair");
        }
        device = descriptors[0];
        peer = descriptors[1];
        timeval timeout{};
        timeout.tv_sec = 2;
        if (::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
        {
            ::close(device);
            ::close(peer);
            throw std::runtime_error("failed to set test receive timeout");
        }
    }

    ~DescriptorPair()
    {
        if (device >= 0)
        {
            ::close(device);
        }
        if (peer >= 0)
        {
            ::close(peer);
        }
    }
};

void test_bidirectional_packet_io_and_stop()
{
    DescriptorPair descriptors;
    boost::asio::io_context io_context;
    auto device = LinuxTunDevice::adopt(io_context, descriptors.device, "test-tun");
    descriptors.device = -1;
    std::promise<std::vector<std::uint8_t>> read_promise;
    auto read_future = read_promise.get_future();
    std::atomic<int> errors{0};
    device->start(
        [&read_promise](std::vector<std::uint8_t> packet)
        { read_promise.set_value(std::move(packet)); },
        [&errors](const boost::system::error_code&) { ++errors; });
    std::thread io_thread([&io_context] { io_context.run(); });

    const std::vector<std::uint8_t> from_kernel{0x45U, 0x00U, 0x00U, 0x14U, 0xABU};
    const auto sent = ::send(descriptors.peer,
                             from_kernel.data(),
                             from_kernel.size(),
                             MSG_NOSIGNAL);
    check(sent == static_cast<ssize_t>(from_kernel.size()),
          "test packet is written to the adopted descriptor");
    check(read_future.wait_for(2s) == std::future_status::ready &&
              read_future.get() == from_kernel,
          "TUN read callback receives one owning packet buffer");

    const std::vector<std::uint8_t> to_kernel{0x45U, 0x00U, 0x00U, 0x14U, 0xCDU};
    device->async_write_packet(to_kernel);
    std::array<std::uint8_t, 64U> receive_buffer{};
    const auto received = ::recv(descriptors.peer,
                                 receive_buffer.data(),
                                 receive_buffer.size(),
                                 0);
    check(received == static_cast<ssize_t>(to_kernel.size()) &&
              std::equal(to_kernel.begin(), to_kernel.end(), receive_buffer.begin()),
          "queued TUN write preserves the complete packet");

    device->stop();
    io_thread.join();
    check(errors.load() == 0, "explicit stop cancels pending I/O without a fatal error");
}

void test_invalid_packet_fails_closed()
{
    DescriptorPair descriptors;
    boost::asio::io_context io_context;
    auto device = LinuxTunDevice::adopt(io_context, descriptors.device, "test-tun");
    descriptors.device = -1;
    std::promise<boost::system::error_code> error_promise;
    auto error_future = error_promise.get_future();
    device->start([](std::vector<std::uint8_t>) {},
                  [&error_promise](const boost::system::error_code& error)
                  { error_promise.set_value(error); });
    device->async_write_packet(
        std::vector<std::uint8_t>(LinuxTunDevice::kMaximumPacketSize + 1U, 0U));
    io_context.run();
    check(error_future.wait_for(0s) == std::future_status::ready &&
              error_future.get() == boost::system::errc::make_error_code(
                                        boost::system::errc::message_size),
          "oversized TUN packet closes the adapter with a stable error");
}

void test_bounded_write_queue_fails_closed()
{
    DescriptorPair descriptors;
    boost::asio::io_context io_context;
    auto device = LinuxTunDevice::adopt(io_context, descriptors.device, "test-tun", 1U, 64U);
    descriptors.device = -1;
    std::promise<boost::system::error_code> error_promise;
    auto error_future = error_promise.get_future();
    device->start([](std::vector<std::uint8_t>) {},
                  [&error_promise](const boost::system::error_code& error)
                  { error_promise.set_value(error); });
    const std::vector<std::uint8_t> packet{0x45U, 0x00U, 0x00U, 0x04U};
    device->async_write_packet(packet);
    device->async_write_packet(packet);
    io_context.run();
    check(error_future.wait_for(0s) == std::future_status::ready &&
              error_future.get() == boost::system::errc::make_error_code(
                                        boost::system::errc::no_buffer_space),
          "full TUN write queue closes the adapter instead of growing unbounded");
}

} // namespace

int main()
{
    test_bidirectional_packet_io_and_stop();
    test_invalid_packet_fails_closed();
    test_bounded_write_queue_fails_closed();

    if (failures != 0)
    {
        std::cerr << failures << " Linux TUN adapter test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Linux TUN adapter tests passed\n";
    return EXIT_SUCCESS;
}
