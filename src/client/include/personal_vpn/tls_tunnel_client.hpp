#ifndef PERSONAL_VPN_TLS_TUNNEL_CLIENT_HPP
#define PERSONAL_VPN_TLS_TUNNEL_CLIENT_HPP

#include "personal_vpn/client_session_controller.hpp"
#include "personal_vpn/outbound_frame_queue.hpp"
#include "personal_vpn/protocol.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace personal_vpn::client
{

class TlsTunnelClient final : public std::enable_shared_from_this<TlsTunnelClient>
{
   public:
    using Tcp = boost::asio::ip::tcp;
    using TlsStream = boost::asio::ssl::stream<Tcp::socket>;
    using AssignmentHandler = std::function<bool(const protocol::IpAssignment&)>;
    using PacketSink = std::function<void(const std::vector<std::uint8_t>&)>;
    using CloseHandler = std::function<void(const std::optional<protocol::ErrorMessage>&)>;

    TlsTunnelClient(TlsStream stream,
                    std::string expected_server_name,
                    AssignmentHandler assignment_handler,
                    PacketSink packet_sink,
                    CloseHandler close_handler,
                    std::uint16_t requested_mtu = 1'400U,
                    std::size_t maximum_queued_frames = 256U,
                    std::size_t maximum_queued_bytes = 1U << 20U);

    void start();
    void send_ipv4_from_tun(std::vector<std::uint8_t> packet);
    void ping(std::uint64_t nonce);
    void stop(std::uint16_t code = 0U);

    [[nodiscard]] TlsStream& stream() noexcept { return stream_; }

   private:
    void start_on_strand();
    void handle_handshake(const boost::system::error_code& error);
    void read_next();
    void handle_read(const boost::system::error_code& error, std::size_t bytes_transferred);
    void apply_result(core::ClientSessionResult result);
    bool enqueue_frame(const protocol::Frame& frame);
    void write_next();
    void handle_write(const boost::system::error_code& error, std::size_t bytes_transferred);
    void close_now();

    TlsStream stream_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::string expected_server_name_;
    AssignmentHandler assignment_handler_;
    PacketSink packet_sink_;
    CloseHandler close_handler_;
    core::ClientSessionController controller_;
    protocol::FrameDecoder decoder_;
    core::OutboundFrameQueue outbound_queue_;
    std::optional<protocol::ErrorMessage> remote_error_;
    std::array<std::uint8_t, 64U * 1024U> read_buffer_{};
    bool started_{false};
    bool write_in_progress_{false};
    bool close_after_write_{false};
    bool stopped_{false};
};

} // namespace personal_vpn::client

#endif
