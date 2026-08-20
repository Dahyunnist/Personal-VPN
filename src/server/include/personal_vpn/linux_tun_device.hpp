#ifndef PERSONAL_VPN_LINUX_TUN_DEVICE_HPP
#define PERSONAL_VPN_LINUX_TUN_DEVICE_HPP

#include <boost/asio.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace personal_vpn::server
{

class LinuxTunDevice final : public std::enable_shared_from_this<LinuxTunDevice>
{
   public:
    using PacketHandler = std::function<void(std::vector<std::uint8_t>)>;
    using ErrorHandler = std::function<void(const boost::system::error_code&)>;

    static constexpr std::size_t kMaximumPacketSize = 65'535U;

    [[nodiscard]] static std::shared_ptr<LinuxTunDevice> open(
        boost::asio::io_context& io_context,
        const std::string& requested_name,
        std::size_t maximum_queued_packets = 256U,
        std::size_t maximum_queued_bytes = 4U << 20U);

    [[nodiscard]] static std::shared_ptr<LinuxTunDevice> adopt(
        boost::asio::io_context& io_context,
        int native_descriptor,
        std::string name,
        std::size_t maximum_queued_packets = 256U,
        std::size_t maximum_queued_bytes = 4U << 20U);

    ~LinuxTunDevice() = default;
    LinuxTunDevice(const LinuxTunDevice&) = delete;
    LinuxTunDevice& operator=(const LinuxTunDevice&) = delete;

    void start(PacketHandler packet_handler, ErrorHandler error_handler);
    void async_write_packet(std::vector<std::uint8_t> packet);
    void stop();

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

   private:
    LinuxTunDevice(boost::asio::io_context& io_context,
                   int native_descriptor,
                   std::string name,
                   std::size_t maximum_queued_packets,
                   std::size_t maximum_queued_bytes);

    void start_on_strand(PacketHandler packet_handler, ErrorHandler error_handler);
    void read_next();
    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred);
    void enqueue_write(std::vector<std::uint8_t> packet);
    void write_next();
    void handle_write(const boost::system::error_code& error,
                      std::size_t bytes_transferred);
    void fail(const boost::system::error_code& error);
    void stop_on_strand();

    boost::asio::posix::stream_descriptor descriptor_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::string name_;
    const std::size_t maximum_queued_packets_;
    const std::size_t maximum_queued_bytes_;
    PacketHandler packet_handler_;
    ErrorHandler error_handler_;
    std::array<std::uint8_t, kMaximumPacketSize> read_buffer_{};
    std::deque<std::vector<std::uint8_t>> write_queue_;
    std::size_t queued_bytes_{0U};
    bool write_in_progress_{false};
    bool started_{false};
    bool stopped_{false};
};

} // namespace personal_vpn::server

#endif
