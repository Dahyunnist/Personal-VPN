#include "personal_vpn/tls_tunnel_client.hpp"

#include "personal_vpn/client_tls_security.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/write.hpp>

#include <exception>
#include <utility>

namespace personal_vpn::client
{

TlsTunnelClient::TlsTunnelClient(TlsStream stream,
                                 std::string expected_server_name,
                                 AssignmentHandler assignment_handler,
                                 PacketSink packet_sink,
                                 CloseHandler close_handler,
                                 const std::uint16_t requested_mtu,
                                 const std::size_t maximum_queued_frames,
                                 const std::size_t maximum_queued_bytes)
    : stream_(std::move(stream)),
      strand_(boost::asio::make_strand(stream_.get_executor())),
      expected_server_name_(std::move(expected_server_name)),
      assignment_handler_(std::move(assignment_handler)),
      packet_sink_(std::move(packet_sink)),
      close_handler_(std::move(close_handler)),
      controller_(requested_mtu),
      outbound_queue_(maximum_queued_frames, maximum_queued_bytes)
{
    if (expected_server_name_.empty())
    {
        throw std::invalid_argument("expected server name must not be empty");
    }
}

void TlsTunnelClient::start()
{
    boost::asio::dispatch(strand_, [self = shared_from_this()] { self->start_on_strand(); });
}

void TlsTunnelClient::send_ipv4_from_tun(std::vector<std::uint8_t> packet)
{
    boost::asio::post(
        strand_,
        [self = shared_from_this(), packet = std::move(packet)]() mutable
        {
            if (self->stopped_ || self->close_after_write_)
            {
                return;
            }
            const auto frame = self->controller_.make_data_to_server(packet);
            if (frame.has_value() && !self->enqueue_frame(*frame))
            {
                self->close_now();
            }
        });
}

void TlsTunnelClient::ping(const std::uint64_t nonce)
{
    boost::asio::post(
        strand_,
        [self = shared_from_this(), nonce]
        {
            if (self->stopped_ || self->close_after_write_)
            {
                return;
            }
            const auto frame = self->controller_.make_ping(nonce);
            if (frame.has_value() && !self->enqueue_frame(*frame))
            {
                self->close_now();
            }
        });
}

void TlsTunnelClient::stop(const std::uint16_t code)
{
    boost::asio::post(
        strand_,
        [self = shared_from_this(), code]
        {
            if (self->stopped_)
            {
                return;
            }
            const auto frame = self->controller_.make_close(code);
            if (!frame.has_value())
            {
                self->close_now();
                return;
            }
            self->close_after_write_ = true;
            if (!self->enqueue_frame(*frame))
            {
                self->close_now();
            }
        });
}

void TlsTunnelClient::start_on_strand()
{
    if (stopped_ || started_)
    {
        return;
    }
    started_ = true;
    try
    {
        configure_client_sni(stream_.native_handle(), expected_server_name_);
    }
    catch (const std::exception&)
    {
        close_now();
        return;
    }
    stream_.async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](const boost::system::error_code& error)
            { self->handle_handshake(error); }));
}

void TlsTunnelClient::handle_handshake(const boost::system::error_code& error)
{
    if (stopped_)
    {
        return;
    }
    if (error)
    {
        close_now();
        return;
    }
    if (!enqueue_frame(controller_.start()))
    {
        close_now();
        return;
    }
    read_next();
}

void TlsTunnelClient::read_next()
{
    if (stopped_ || close_after_write_)
    {
        return;
    }
    stream_.async_read_some(
        boost::asio::buffer(read_buffer_),
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](const boost::system::error_code& error,
                                       const std::size_t bytes_transferred)
            { self->handle_read(error, bytes_transferred); }));
}

void TlsTunnelClient::handle_read(const boost::system::error_code& error,
                                  const std::size_t bytes_transferred)
{
    if (stopped_)
    {
        return;
    }
    if (error)
    {
        close_now();
        return;
    }
    try
    {
        const auto frames = decoder_.push(read_buffer_.data(), bytes_transferred);
        for (const auto& frame : frames)
        {
            apply_result(controller_.handle(frame));
            if (stopped_ || close_after_write_)
            {
                break;
            }
        }
    }
    catch (const std::exception&)
    {
        close_now();
        return;
    }
    if (!stopped_ && !close_after_write_)
    {
        read_next();
    }
}

void TlsTunnelClient::apply_result(core::ClientSessionResult result)
{
    if (result.remote_error.has_value())
    {
        remote_error_ = std::move(result.remote_error);
    }
    if (result.assignment.has_value() && assignment_handler_ &&
        !assignment_handler_(*result.assignment))
    {
        close_now();
        return;
    }
    for (const auto& packet : result.packets_to_tun)
    {
        if (packet_sink_)
        {
            packet_sink_(packet);
        }
    }
    for (const auto& frame : result.outbound_frames)
    {
        if (!enqueue_frame(frame))
        {
            close_now();
            return;
        }
    }
    if (result.close_transport)
    {
        close_after_write_ = true;
        if (!write_in_progress_ && outbound_queue_.empty())
        {
            close_now();
        }
    }
}

bool TlsTunnelClient::enqueue_frame(const protocol::Frame& frame)
{
    if (stopped_ || !outbound_queue_.try_enqueue(frame))
    {
        return false;
    }
    if (!write_in_progress_)
    {
        write_next();
    }
    return true;
}

void TlsTunnelClient::write_next()
{
    if (stopped_ || outbound_queue_.empty())
    {
        write_in_progress_ = false;
        if (close_after_write_)
        {
            close_now();
        }
        return;
    }
    write_in_progress_ = true;
    const auto buffer = outbound_queue_.front();
    boost::asio::async_write(
        stream_,
        boost::asio::buffer(*buffer),
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this(), buffer](const boost::system::error_code& error,
                                                const std::size_t bytes_transferred)
            { self->handle_write(error, bytes_transferred); }));
}

void TlsTunnelClient::handle_write(const boost::system::error_code& error,
                                   const std::size_t /*bytes_transferred*/)
{
    if (stopped_)
    {
        return;
    }
    if (error)
    {
        close_now();
        return;
    }
    outbound_queue_.pop_front();
    write_next();
}

void TlsTunnelClient::close_now()
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    controller_.on_transport_closed();
    outbound_queue_.clear();
    boost::system::error_code ignored;
    stream_.lowest_layer().cancel(ignored);
    stream_.lowest_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    stream_.lowest_layer().close(ignored);
    if (close_handler_)
    {
        auto handler = std::move(close_handler_);
        handler(remote_error_);
    }
}

} // namespace personal_vpn::client
