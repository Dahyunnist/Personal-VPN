#include "personal_vpn/tls_tunnel_session.hpp"

#include "personal_vpn/tls_security.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/write.hpp>

#include <exception>
#include <utility>

namespace personal_vpn::server
{

TlsTunnelSession::TlsTunnelSession(TlsStream stream,
                                   core::LeaseManager& lease_manager,
                                   PacketSink packet_sink,
                                   EstablishedHandler established_handler,
                                   CloseHandler close_handler,
                                   const std::size_t maximum_queued_frames,
                                   const std::size_t maximum_queued_bytes,
                                   const std::chrono::milliseconds handshake_timeout,
                                   const std::chrono::milliseconds idle_timeout,
                                   ServerMetrics* const metrics)
    : stream_(std::move(stream)),
      strand_(boost::asio::make_strand(stream_.get_executor())),
      lease_manager_(lease_manager),
      packet_sink_(std::move(packet_sink)),
      established_handler_(std::move(established_handler)),
      close_handler_(std::move(close_handler)),
      outbound_queue_(maximum_queued_frames, maximum_queued_bytes),
      deadline_timer_(stream_.get_executor()),
      handshake_timeout_(handshake_timeout),
      idle_timeout_(idle_timeout),
      metrics_(metrics)
{
    if (handshake_timeout_ <= std::chrono::milliseconds::zero() ||
        idle_timeout_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("TLS session timeouts must be positive");
    }
}

void TlsTunnelSession::start()
{
    boost::asio::dispatch(strand_, [self = shared_from_this()] { self->start_on_strand(); });
}

void TlsTunnelSession::send_ipv4_from_tun(std::vector<std::uint8_t> packet)
{
    boost::asio::post(
        strand_,
        [self = shared_from_this(), packet = std::move(packet)]() mutable
        {
            if (self->stopped_ || !self->controller_)
            {
                return;
            }
            const auto frame = self->controller_->make_data_to_client(packet);
            if (!frame.has_value())
            {
                if (self->controller_->state() != core::SessionState::Established)
                {
                    self->close_now();
                }
                return;
            }
            if (!self->enqueue_frame(*frame))
            {
                if (self->metrics_)
                {
                    ++self->metrics_->queue_overflows;
                }
                self->close_now();
            }
            else if (self->metrics_)
            {
                ++self->metrics_->downlink_packets;
                self->metrics_->downlink_bytes.fetch_add(packet.size());
            }
        });
}

void TlsTunnelSession::stop()
{
    boost::asio::post(strand_, [self = shared_from_this()] { self->close_now(); });
}

void TlsTunnelSession::start_on_strand()
{
    if (stopped_ || controller_)
    {
        return;
    }
    deadline_timer_.expires_after(handshake_timeout_);
    deadline_timer_.async_wait(boost::asio::bind_executor(
        strand_,
        [self = shared_from_this()](const boost::system::error_code& error)
        {
            if (!error && !self->stopped_ && !self->controller_)
            {
                if (self->metrics_)
                {
                    ++self->metrics_->handshake_timeouts;
                }
                self->close_now();
            }
        }));
    stream_.async_handshake(
        boost::asio::ssl::stream_base::server,
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](const boost::system::error_code& error)
            { self->handle_handshake(error); }));
}

void TlsTunnelSession::handle_handshake(const boost::system::error_code& error)
{
    if (stopped_)
    {
        return;
    }
    if (error)
    {
        if (metrics_)
        {
            ++metrics_->handshake_failures;
        }
        close_now();
        return;
    }
    try
    {
        controller_ = std::make_unique<core::SessionController>(
            lease_manager_, peer_certificate_sha256(stream_.native_handle()));
        static_cast<void>(deadline_timer_.cancel());
        arm_idle_timeout();
        read_next();
    }
    catch (const std::exception&)
    {
        close_now();
    }
}

void TlsTunnelSession::read_next()
{
    if (stopped_)
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

void TlsTunnelSession::handle_read(const boost::system::error_code& error,
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
            arm_idle_timeout();
            auto result = controller_->handle(frame);
            if (!route_address_ && controller_->state() == core::SessionState::Established &&
                controller_->lease() != nullptr)
            {
                route_address_ = controller_->lease()->address;
                if (established_handler_ &&
                    !established_handler_(*route_address_, shared_from_this()))
                {
                    close_now();
                    return;
                }
                if (!established_counted_ && metrics_)
                {
                    ++metrics_->established_sessions;
                    established_counted_ = true;
                }
            }
            apply_result(std::move(result));
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

void TlsTunnelSession::apply_result(core::SessionResult result)
{
    for (const auto& packet : result.packets_to_tun)
    {
        if (metrics_)
        {
            ++metrics_->uplink_packets;
            metrics_->uplink_bytes.fetch_add(packet.size());
        }
        if (packet_sink_)
        {
            packet_sink_(packet);
        }
    }
    for (const auto& frame : result.outbound_frames)
    {
        if (!enqueue_frame(frame))
        {
            if (metrics_)
            {
                ++metrics_->queue_overflows;
            }
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

void TlsTunnelSession::arm_idle_timeout()
{
    deadline_timer_.expires_after(idle_timeout_);
    deadline_timer_.async_wait(boost::asio::bind_executor(
        strand_,
        [self = shared_from_this()](const boost::system::error_code& error)
        {
            if (!error && !self->stopped_)
            {
                if (self->metrics_)
                {
                    ++self->metrics_->idle_timeouts;
                }
                self->close_now();
            }
        }));
}

bool TlsTunnelSession::enqueue_frame(const protocol::Frame& frame)
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

void TlsTunnelSession::write_next()
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

void TlsTunnelSession::handle_write(const boost::system::error_code& error,
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

void TlsTunnelSession::close_now()
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    static_cast<void>(deadline_timer_.cancel());
    if (metrics_)
    {
        ++metrics_->closed_sessions;
    }
    if (controller_)
    {
        controller_->on_transport_closed();
    }
    outbound_queue_.clear();
    boost::system::error_code ignored;
    stream_.lowest_layer().cancel(ignored);
    stream_.lowest_layer().shutdown(Tcp::socket::shutdown_both, ignored);
    stream_.lowest_layer().close(ignored);
    if (close_handler_)
    {
        auto handler = std::move(close_handler_);
        handler(route_address_, this);
    }
}

} // namespace personal_vpn::server
