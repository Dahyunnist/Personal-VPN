#ifndef PERSONAL_VPN_CLIENT_RUNTIME_HPP
#define PERSONAL_VPN_CLIENT_RUNTIME_HPP

#include "personal_vpn/client_config.hpp"
#include "personal_vpn/client_network_transaction.hpp"
#include "personal_vpn/client_tls_security.hpp"
#include "personal_vpn/tls_tunnel_client.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace personal_vpn::client
{

enum class ClientRuntimeState
{
    Stopped,
    Starting,
    Resolving,
    Connecting,
    ConfiguringNetwork,
    Connected,
    Stopping,
    Failed,
};

struct ClientRuntimeEvent
{
    ClientRuntimeState state{ClientRuntimeState::Stopped};
    std::string message;
};

class ClientRuntime final
{
   public:
    using EventHandler = std::function<void(const ClientRuntimeEvent&)>;

    ClientRuntime(ClientConfig config,
                  std::unique_ptr<ClientPacketDevice> packet_device,
                  EventHandler event_handler = {},
                  std::chrono::milliseconds connection_timeout = std::chrono::seconds(15));
    ~ClientRuntime();

    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;

    void start();
    void stop();
    void wait();

    [[nodiscard]] ClientRuntimeState state() const noexcept { return state_.load(); }
    [[nodiscard]] std::string failure_message() const;

   private:
    using Tcp = boost::asio::ip::tcp;
    using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    void begin_connect();
    void handle_resolve(const boost::system::error_code& error,
                        const Tcp::resolver::results_type& endpoints);
    void handle_connect(const boost::system::error_code& error);
    bool configure_network(const protocol::IpAssignment& assignment);
    void deliver_to_packet_device(const std::vector<std::uint8_t>& packet);
    void run_packet_reader(std::shared_ptr<TlsTunnelClient> tunnel);
    void on_tunnel_closed(const std::optional<protocol::ErrorMessage>& remote_error);
    void fail_on_io(std::string message);
    void finish_on_io();
    void transition(ClientRuntimeState state, std::string message = {});
    void record_failure(const std::string& message);

    ClientConfig config_;
    std::unique_ptr<ClientPacketDevice> packet_device_;
    EventHandler event_handler_;
    std::chrono::milliseconds connection_timeout_;
    boost::asio::io_context io_context_;
    boost::asio::ssl::context tls_context_;
    Tcp::resolver resolver_;
    boost::asio::steady_timer connection_timer_;
    std::optional<WorkGuard> work_guard_;
    std::unique_ptr<TlsTunnelClient::TlsStream> pending_stream_;
    std::shared_ptr<TlsTunnelClient> tunnel_;
    std::unique_ptr<ClientNetworkTransaction> network_transaction_;
    std::thread io_thread_;
    std::thread packet_thread_;
    std::atomic<ClientRuntimeState> state_{ClientRuntimeState::Stopped};
    std::atomic<bool> started_{false};
    std::atomic<bool> intentional_stop_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> finished_{false};
    mutable std::mutex failure_mutex_;
    std::string failure_message_;
};

} // namespace personal_vpn::client

#endif
