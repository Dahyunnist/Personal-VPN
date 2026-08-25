#include "personal_vpn/linux_tun_device.hpp"

#include <boost/asio/bind_executor.hpp>

#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace personal_vpn::server
{
std::shared_ptr<LinuxTunDevice> LinuxTunDevice::open(
    boost::asio::io_context& io_context,
    const std::string& requested_name,
    const std::size_t maximum_queued_packets,
    const std::size_t maximum_queued_bytes)
{
    if (requested_name.size() >= IFNAMSIZ)
    {
        throw std::invalid_argument("TUN interface name is too long");
    }
    if (maximum_queued_packets == 0U || maximum_queued_bytes == 0U)
    {
        throw std::invalid_argument("TUN write queue limits must be positive");
    }
    const int descriptor = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0)
    {
        throw std::system_error(errno, std::generic_category(), "failed to open /dev/net/tun");
    }

    ifreq request{};
    request.ifr_flags = static_cast<short>(IFF_TUN | IFF_NO_PI);
    std::memcpy(request.ifr_name, requested_name.data(), requested_name.size());
    if (::ioctl(descriptor, TUNSETIFF, &request) < 0)
    {
        const int error = errno;
        ::close(descriptor);
        throw std::system_error(error, std::generic_category(), "failed to attach TUN interface");
    }

    return adopt(io_context,
                 descriptor,
                 request.ifr_name,
                 maximum_queued_packets,
                 maximum_queued_bytes);
}

std::shared_ptr<LinuxTunDevice> LinuxTunDevice::adopt(
    boost::asio::io_context& io_context,
    const int native_descriptor,
    std::string name,
    const std::size_t maximum_queued_packets,
    const std::size_t maximum_queued_bytes)
{
    if (native_descriptor < 0)
    {
        throw std::invalid_argument("TUN descriptor must be valid");
    }
    if (name.empty())
    {
        throw std::invalid_argument("TUN interface name must not be empty");
    }
    if (maximum_queued_packets == 0U || maximum_queued_bytes == 0U)
    {
        throw std::invalid_argument("TUN write queue limits must be positive");
    }
    return std::shared_ptr<LinuxTunDevice>(new LinuxTunDevice(io_context,
                                                              native_descriptor,
                                                              std::move(name),
                                                              maximum_queued_packets,
                                                              maximum_queued_bytes));
}

LinuxTunDevice::LinuxTunDevice(boost::asio::io_context& io_context,
                               const int native_descriptor,
                               std::string name,
                               const std::size_t maximum_queued_packets,
                               const std::size_t maximum_queued_bytes)
    : descriptor_(io_context, native_descriptor),
      strand_(boost::asio::make_strand(io_context)),
      name_(std::move(name)),
      maximum_queued_packets_(maximum_queued_packets),
      maximum_queued_bytes_(maximum_queued_bytes)
{
}

void LinuxTunDevice::start(PacketHandler packet_handler, ErrorHandler error_handler)
{
    if (!packet_handler || !error_handler)
    {
        throw std::invalid_argument("TUN packet and error handlers must be configured");
    }
    boost::asio::dispatch(
        strand_,
        [self = shared_from_this(),
         packet_handler = std::move(packet_handler),
         error_handler = std::move(error_handler)]() mutable
        {
            self->start_on_strand(std::move(packet_handler), std::move(error_handler));
        });
}

void LinuxTunDevice::async_write_packet(std::vector<std::uint8_t> packet)
{
    boost::asio::post(
        strand_,
        [self = shared_from_this(), packet = std::move(packet)]() mutable
        { self->enqueue_write(std::move(packet)); });
}

void LinuxTunDevice::stop()
{
    boost::asio::post(strand_, [self = shared_from_this()] { self->stop_on_strand(); });
}

void LinuxTunDevice::start_on_strand(PacketHandler packet_handler, ErrorHandler error_handler)
{
    if (stopped_)
    {
        return;
    }
    if (started_)
    {
        fail(make_error_code(boost::system::errc::invalid_argument));
        return;
    }
    started_ = true;
    packet_handler_ = std::move(packet_handler);
    error_handler_ = std::move(error_handler);
    read_next();
}

void LinuxTunDevice::read_next()
{
    descriptor_.async_read_some(
        boost::asio::buffer(read_buffer_),
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](const boost::system::error_code& error,
                                       const std::size_t bytes_transferred)
            { self->handle_read(error, bytes_transferred); }));
}

void LinuxTunDevice::handle_read(const boost::system::error_code& error,
                                 const std::size_t bytes_transferred)
{
    if (stopped_)
    {
        return;
    }
    if (error)
    {
        fail(error);
        return;
    }
    if (bytes_transferred == 0U)
    {
        fail(make_error_code(boost::system::errc::io_error));
        return;
    }
    std::vector<std::uint8_t> packet(read_buffer_.begin(),
                                     read_buffer_.begin() +
                                         static_cast<std::ptrdiff_t>(bytes_transferred));
    packet_handler_(std::move(packet));
    read_next();
}

void LinuxTunDevice::enqueue_write(std::vector<std::uint8_t> packet)
{
    if (stopped_)
    {
        return;
    }
    if (!started_)
    {
        fail(make_error_code(boost::system::errc::not_connected));
        return;
    }
    if (packet.empty() || packet.size() > kMaximumPacketSize)
    {
        fail(make_error_code(boost::system::errc::message_size));
        return;
    }
    if (write_queue_.size() >= maximum_queued_packets_ ||
        packet.size() > maximum_queued_bytes_ - queued_bytes_)
    {
        fail(make_error_code(boost::system::errc::no_buffer_space));
        return;
    }
    queued_bytes_ += packet.size();
    write_queue_.push_back(std::move(packet));
    if (!write_in_progress_)
    {
        write_next();
    }
}

void LinuxTunDevice::write_next()
{
    if (stopped_ || write_queue_.empty())
    {
        write_in_progress_ = false;
        return;
    }
    write_in_progress_ = true;
    descriptor_.async_write_some(
        boost::asio::buffer(write_queue_.front()),
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](const boost::system::error_code& error,
                                       const std::size_t bytes_transferred)
            { self->handle_write(error, bytes_transferred); }));
}

void LinuxTunDevice::handle_write(const boost::system::error_code& error,
                                  const std::size_t bytes_transferred)
{
    if (stopped_)
    {
        return;
    }
    if (error)
    {
        fail(error);
        return;
    }
    if (write_queue_.empty() || bytes_transferred != write_queue_.front().size())
    {
        fail(make_error_code(boost::system::errc::io_error));
        return;
    }
    queued_bytes_ -= write_queue_.front().size();
    write_queue_.pop_front();
    write_next();
}

void LinuxTunDevice::fail(const boost::system::error_code& error)
{
    if (stopped_)
    {
        return;
    }
    auto handler = std::move(error_handler_);
    stop_on_strand();
    if (handler)
    {
        handler(error);
    }
}

void LinuxTunDevice::stop_on_strand()
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    write_queue_.clear();
    queued_bytes_ = 0U;
    packet_handler_ = {};
    error_handler_ = {};
    boost::system::error_code ignored;
    descriptor_.cancel(ignored);
    descriptor_.close(ignored);
}

} // namespace personal_vpn::server
