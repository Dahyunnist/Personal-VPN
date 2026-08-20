#include "personal_vpn/tls_tunnel_server.hpp"

#include <boost/asio/bind_executor.hpp>

#include <stdexcept>
#include <utility>

namespace personal_vpn::server
{

TlsTunnelServer::TlsTunnelServer(boost::asio::io_context& io_context,
                                 Tcp::endpoint listen_endpoint,
                                 boost::asio::ssl::context& tls_context,
                                 core::LeaseManager& lease_manager,
                                 std::shared_ptr<LinuxTunDevice> tun_device,
                                 const std::size_t maximum_sessions)
    : strand_(boost::asio::make_strand(io_context)),
      acceptor_(io_context),
      tls_context_(tls_context),
      lease_manager_(lease_manager),
      tun_device_(std::move(tun_device)),
      maximum_sessions_(maximum_sessions)
{
    if (!tun_device_)
    {
        throw std::invalid_argument("TUN device must not be null");
    }
    if (maximum_sessions_ == 0U)
    {
        throw std::invalid_argument("maximum session count must be positive");
    }
    acceptor_.open(listen_endpoint.protocol());
    acceptor_.set_option(Tcp::acceptor::reuse_address(true));
    acceptor_.bind(listen_endpoint);
    acceptor_.listen(boost::asio::socket_base::max_listen_connections);
}

void TlsTunnelServer::start()
{
    boost::asio::dispatch(strand_, [self = shared_from_this()] { self->start_on_strand(); });
}

void TlsTunnelServer::stop()
{
    boost::asio::post(strand_, [self = shared_from_this()] { self->stop_on_strand(); });
}

TlsTunnelServer::Tcp::endpoint TlsTunnelServer::local_endpoint() const
{
    boost::system::error_code error;
    const auto endpoint = acceptor_.local_endpoint(error);
    if (error)
    {
        throw boost::system::system_error(error);
    }
    return endpoint;
}

void TlsTunnelServer::start_on_strand()
{
    if (started_ || stopping_)
    {
        return;
    }
    started_ = true;
    std::weak_ptr<TlsTunnelServer> weak_self = shared_from_this();
    tun_device_->start(
        [weak_self](std::vector<std::uint8_t> packet)
        {
            if (const auto self = weak_self.lock())
            {
                static_cast<void>(self->router_.route_ipv4_packet(std::move(packet)));
            }
        },
        [weak_self](const boost::system::error_code&)
        {
            if (const auto self = weak_self.lock())
            {
                self->stop();
            }
        });
    accept_next();
}

void TlsTunnelServer::accept_next()
{
    if (stopping_)
    {
        return;
    }
    acceptor_.async_accept(
        boost::asio::bind_executor(
            strand_,
            [self = shared_from_this()](const boost::system::error_code& error,
                                       Tcp::socket socket) mutable
            { self->handle_accept(error, std::move(socket)); }));
}

void TlsTunnelServer::handle_accept(const boost::system::error_code& error, Tcp::socket socket)
{
    if (stopping_)
    {
        return;
    }
    if (error)
    {
        stop_on_strand();
        return;
    }
    if (sessions_.size() >= maximum_sessions_)
    {
        boost::system::error_code ignored;
        socket.shutdown(Tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    }
    else
    {
        std::weak_ptr<TlsTunnelServer> weak_self = shared_from_this();
        auto session = std::make_shared<TlsTunnelSession>(
            TlsTunnelSession::TlsStream(std::move(socket), tls_context_),
            lease_manager_,
            [weak_self](const std::vector<std::uint8_t>& packet)
            {
                if (const auto self = weak_self.lock())
                {
                    self->tun_device_->async_write_packet(packet);
                }
            },
            [weak_self](const core::Ipv4Address& address,
                        const std::shared_ptr<core::TunnelPeer>& peer)
            {
                if (const auto self = weak_self.lock())
                {
                    return self->router_.register_peer(address, peer);
                }
                return false;
            },
            [weak_self](const std::optional<core::Ipv4Address>& address,
                        const core::TunnelPeer* peer)
            {
                if (const auto self = weak_self.lock())
                {
                    self->handle_session_closed(address, peer);
                }
            });
        sessions_.insert(session);
        active_session_count_.store(sessions_.size());
        session->start();
    }
    accept_next();
}

void TlsTunnelServer::handle_session_closed(
    const std::optional<core::Ipv4Address>& address,
    const core::TunnelPeer* peer)
{
    if (address)
    {
        static_cast<void>(router_.unregister_peer(*address, peer));
    }
    boost::asio::post(strand_,
                      [self = shared_from_this(), peer] { self->remove_session(peer); });
}

void TlsTunnelServer::remove_session(const core::TunnelPeer* peer)
{
    for (auto current = sessions_.begin(); current != sessions_.end(); ++current)
    {
        if (current->get() == peer)
        {
            sessions_.erase(current);
            active_session_count_.store(sessions_.size());
            return;
        }
    }
}

void TlsTunnelServer::stop_on_strand()
{
    if (stopping_)
    {
        return;
    }
    stopping_ = true;
    boost::system::error_code ignored;
    acceptor_.cancel(ignored);
    acceptor_.close(ignored);
    tun_device_->stop();
    for (const auto& session : sessions_)
    {
        session->stop();
    }
}

} // namespace personal_vpn::server
