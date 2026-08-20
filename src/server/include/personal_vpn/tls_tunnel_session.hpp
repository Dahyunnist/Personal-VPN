#ifndef PERSONAL_VPN_TLS_TUNNEL_SESSION_HPP
#define PERSONAL_VPN_TLS_TUNNEL_SESSION_HPP

#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/outbound_frame_queue.hpp"
#include "personal_vpn/protocol.hpp"
#include "personal_vpn/session_controller.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace personal_vpn::server
{

class TlsTunnelSession : public std::enable_shared_from_this<TlsTunnelSession>
{
   public:
    using Tcp = boost::asio::ip::tcp;
    using TlsStream = boost::asio::ssl::stream<Tcp::socket>;
    using PacketSink = std::function<void(const std::vector<std::uint8_t>&)>;
    using CloseHandler = std::function<void()>;

    TlsTunnelSession(TlsStream stream,
                     core::LeaseManager& lease_manager,
                     PacketSink packet_sink,
                     CloseHandler close_handler,
                     std::size_t maximum_queued_frames = 256U,
                     std::size_t maximum_queued_bytes = 1U << 20U);

    void start();
    void send_ipv4_from_tun(std::vector<std::uint8_t> packet);
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
    void close_now();

    TlsStream stream_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    core::LeaseManager& lease_manager_;
    PacketSink packet_sink_;
    CloseHandler close_handler_;
    protocol::FrameDecoder decoder_;
    core::OutboundFrameQueue outbound_queue_;
    std::unique_ptr<core::SessionController> controller_;
    std::array<std::uint8_t, 64U * 1024U> read_buffer_{};
    bool write_in_progress_{false};
    bool close_after_write_{false};
    bool stopped_{false};
};

} // namespace personal_vpn::server

#endif
