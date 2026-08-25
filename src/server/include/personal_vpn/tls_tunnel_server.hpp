#ifndef PERSONAL_VPN_TLS_TUNNEL_SERVER_HPP
#define PERSONAL_VPN_TLS_TUNNEL_SERVER_HPP

#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/linux_tun_device.hpp"
#include "personal_vpn/tls_tunnel_session.hpp"
#include "personal_vpn/server_metrics.hpp"
#include "personal_vpn/tunnel_router.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <atomic>
#include <cstddef>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>

namespace personal_vpn::server
{

class TlsTunnelServer final : public std::enable_shared_from_this<TlsTunnelServer>
{
   public:
    using Tcp = boost::asio::ip::tcp;
    using ShutdownHandler = std::function<void()>;

    TlsTunnelServer(boost::asio::io_context& io_context,
                    Tcp::endpoint listen_endpoint,
                    boost::asio::ssl::context& tls_context,
                    core::LeaseManager& lease_manager,
                    std::shared_ptr<LinuxTunDevice> tun_device,
                    std::size_t maximum_sessions = 1'024U,
                    ShutdownHandler shutdown_handler = {},
                    std::chrono::milliseconds handshake_timeout = std::chrono::seconds(10),
                    std::chrono::milliseconds idle_timeout = std::chrono::minutes(5));

    void start();
    void stop();

    [[nodiscard]] Tcp::endpoint local_endpoint() const;
    [[nodiscard]] std::size_t active_session_count() const noexcept
    {
        return active_session_count_.load();
    }
    [[nodiscard]] std::size_t active_route_count() { return router_.active_routes(); }
    [[nodiscard]] ServerMetricsSnapshot metrics_snapshot() const noexcept
    {
        return metrics_.snapshot();
    }

   private:
    void start_on_strand();
    void accept_next();
    void handle_accept(const boost::system::error_code& error, Tcp::socket socket);
    void handle_session_closed(const std::optional<core::Ipv4Address>& address,
                               const core::TunnelPeer* peer);
    void remove_session(const core::TunnelPeer* peer);
    void stop_on_strand();

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    Tcp::acceptor acceptor_;
    boost::asio::ssl::context& tls_context_;
    core::LeaseManager& lease_manager_;
    std::shared_ptr<LinuxTunDevice> tun_device_;
    const std::size_t maximum_sessions_;
    const std::chrono::milliseconds handshake_timeout_;
    const std::chrono::milliseconds idle_timeout_;
    ShutdownHandler shutdown_handler_;
    core::TunnelRouter router_;
    std::unordered_set<std::shared_ptr<TlsTunnelSession>> sessions_;
    std::atomic<std::size_t> active_session_count_{0U};
    ServerMetrics metrics_;
    bool started_{false};
    bool stopping_{false};
};

} // namespace personal_vpn::server

#endif
