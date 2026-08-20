#ifndef PERSONAL_VPN_TLS_TUNNEL_SESSION_HPP
#define PERSONAL_VPN_TLS_TUNNEL_SESSION_HPP

#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/outbound_frame_queue.hpp"
#include "personal_vpn/protocol.hpp"
#include "personal_vpn/session_controller.hpp"
#include "personal_vpn/server_metrics.hpp"
#include "personal_vpn/tunnel_router.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace personal_vpn::server
{

class TlsTunnelSession final : public core::TunnelPeer,
                               public std::enable_shared_from_this<TlsTunnelSession>
{
   public:
    using Tcp = boost::asio::ip::tcp;
    using TlsStream = boost::asio::ssl::stream<Tcp::socket>;
    using PacketSink = std::function<void(const std::vector<std::uint8_t>&)>;
    using EstablishedHandler =
        std::function<bool(const core::Ipv4Address&, const std::shared_ptr<core::TunnelPeer>&)>;
    using CloseHandler =
        std::function<void(const std::optional<core::Ipv4Address>&, const core::TunnelPeer*)>;

    TlsTunnelSession(TlsStream stream,
                     core::LeaseManager& lease_manager,
                     PacketSink packet_sink,
                     EstablishedHandler established_handler,
                     CloseHandler close_handler,
                     std::size_t maximum_queued_frames = 256U,
                     std::size_t maximum_queued_bytes = 1U << 20U,
                     std::chrono::milliseconds handshake_timeout = std::chrono::seconds(10),
                     std::chrono::milliseconds idle_timeout = std::chrono::minutes(5),
                     ServerMetrics* metrics = nullptr);

    void start();
    void send_ipv4_from_tun(std::vector<std::uint8_t> packet) override;
    void stop();

    [[nodiscard]] TlsStream& stream() noexcept { return stream_; }

   private:
    void start_on_strand();
    void handle_handshake(const boost::system::error_code& error);
    void read_next();
    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred);
    void apply_result(core::SessionResult result);
    bool enqueue_frame(const protocol::Frame& frame);
    void write_next();
    void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred);
    void arm_idle_timeout();
    void close_now();

    TlsStream stream_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    core::LeaseManager& lease_manager_;
    PacketSink packet_sink_;
    EstablishedHandler established_handler_;
    CloseHandler close_handler_;
    protocol::FrameDecoder decoder_;
    core::OutboundFrameQueue outbound_queue_;
    boost::asio::steady_timer deadline_timer_;
    const std::chrono::milliseconds handshake_timeout_;
    const std::chrono::milliseconds idle_timeout_;
    ServerMetrics* metrics_;
    std::unique_ptr<core::SessionController> controller_;
    std::optional<core::Ipv4Address> route_address_;
    std::array<std::uint8_t, 64U * 1024U> read_buffer_{};
    bool write_in_progress_{false};
    bool close_after_write_{false};
    bool stopped_{false};
    bool established_counted_{false};
};

} // namespace personal_vpn::server

#endif
