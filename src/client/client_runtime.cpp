#include "personal_vpn/client_runtime.hpp"

#include <boost/asio/connect.hpp>

#include <exception>
#include <stdexcept>
#include <utility>

namespace personal_vpn::client
{

ClientRuntime::ClientRuntime(ClientConfig config,
                             std::unique_ptr<ClientPacketDevice> packet_device,
                             EventHandler event_handler,
                             const std::chrono::milliseconds connection_timeout)
    : config_(std::move(config)),
      packet_device_(std::move(packet_device)),
      event_handler_(std::move(event_handler)),
      connection_timeout_(connection_timeout),
      tls_context_(make_client_tls_context(config_)),
      resolver_(io_context_),
      connection_timer_(io_context_)
{
    if (!packet_device_)
    {
        throw std::invalid_argument("client runtime packet device must not be null");
    }
    if (connection_timeout_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("client connection timeout must be positive");
    }
    network_transaction_ = std::make_unique<ClientNetworkTransaction>(*packet_device_);
}

ClientRuntime::~ClientRuntime()
{
    stop();
    wait();
}

void ClientRuntime::start()
{
    if (started_.exchange(true))
    {
        throw std::logic_error("client runtime instances are single-use");
    }
    transition(ClientRuntimeState::Starting, "client runtime starting");
    work_guard_.emplace(io_context_.get_executor());
    io_thread_ = std::thread([this]
                             {
                                 try
                                 {
                                     io_context_.run();
                                 }
                                 catch (const std::exception& error)
                                 {
                                     record_failure(std::string("I/O runtime failure: ") +
                                                    error.what());
                                 }
                             });
    boost::asio::post(io_context_, [this] { begin_connect(); });
}

void ClientRuntime::stop()
{
    if (!started_.load() || finished_.load())
    {
        return;
    }
    intentional_stop_ = true;
    stop_requested_ = true;
    transition(ClientRuntimeState::Stopping, "disconnect requested");
    packet_device_->interrupt_receive();
    boost::asio::post(
        io_context_,
        [this]
        {
            resolver_.cancel();
            boost::system::error_code ignored;
            static_cast<void>(connection_timer_.cancel());
            if (tunnel_)
            {
                tunnel_->stop();
            }
            else
            {
                if (pending_stream_)
                {
                    pending_stream_->lowest_layer().cancel(ignored);
                    pending_stream_->lowest_layer().close(ignored);
                }
                finish_on_io();
            }
        });
}

void ClientRuntime::wait()
{
    if (io_thread_.joinable() && io_thread_.get_id() != std::this_thread::get_id())
    {
        io_thread_.join();
    }
}

std::string ClientRuntime::failure_message() const
{
    std::lock_guard<std::mutex> lock(failure_mutex_);
    return failure_message_;
}

void ClientRuntime::begin_connect()
{
    if (stop_requested_)
    {
        finish_on_io();
        return;
    }
    transition(ClientRuntimeState::Resolving, "resolving VPN server");
    connection_timer_.expires_after(connection_timeout_);
    connection_timer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            if (!error && !finished_.load())
            {
                fail_on_io("connection timed out before tunnel establishment");
            }
        });
    resolver_.async_resolve(
        config_.server_host,
        std::to_string(config_.server_port),
        [this](const boost::system::error_code& error,
               const Tcp::resolver::results_type& endpoints)
        { handle_resolve(error, endpoints); });
}

void ClientRuntime::handle_resolve(const boost::system::error_code& error,
                                   const Tcp::resolver::results_type& endpoints)
{
    if (stop_requested_)
    {
        finish_on_io();
        return;
    }
    if (error)
    {
        fail_on_io("server name resolution failed: " + error.message());
        return;
    }
    transition(ClientRuntimeState::Connecting, "connecting with mutual TLS");
    pending_stream_ =
        std::make_unique<TlsTunnelClient::TlsStream>(io_context_, tls_context_);
    boost::asio::async_connect(
        pending_stream_->lowest_layer(),
        endpoints,
        [this](const boost::system::error_code& connect_error, const Tcp::endpoint&)
        { handle_connect(connect_error); });
}

void ClientRuntime::handle_connect(const boost::system::error_code& error)
{
    if (stop_requested_)
    {
        finish_on_io();
        return;
    }
    if (error)
    {
        fail_on_io("TCP connection failed: " + error.message());
        return;
    }
    tunnel_ = std::make_shared<TlsTunnelClient>(
        std::move(*pending_stream_),
        config_.expected_server_name,
        [this](const protocol::IpAssignment& assignment)
        { return configure_network(assignment); },
        [this](const std::vector<std::uint8_t>& packet)
        { deliver_to_packet_device(packet); },
        [this](const std::optional<protocol::ErrorMessage>& remote_error)
        { on_tunnel_closed(remote_error); },
        config_.requested_mtu);
    pending_stream_.reset();
    tunnel_->start();
}

bool ClientRuntime::configure_network(const protocol::IpAssignment& assignment)
{
    if (stop_requested_)
    {
        return false;
    }
    transition(ClientRuntimeState::ConfiguringNetwork, "applying tunnel network transaction");
    try
    {
        network_transaction_->activate(assignment, config_.routes);
    }
    catch (const std::exception& error)
    {
        record_failure(std::string("network configuration failed: ") + error.what());
        return false;
    }
    static_cast<void>(connection_timer_.cancel());
    transition(ClientRuntimeState::Connected, "VPN tunnel connected");
    packet_thread_ = std::thread([this, tunnel = tunnel_]
                                 { run_packet_reader(std::move(tunnel)); });
    return true;
}

void ClientRuntime::deliver_to_packet_device(const std::vector<std::uint8_t>& packet)
{
    try
    {
        packet_device_->send_packet(packet);
    }
    catch (const std::exception& error)
    {
        record_failure(std::string("virtual adapter write failed: ") + error.what());
        stop_requested_ = true;
        packet_device_->interrupt_receive();
        if (tunnel_)
        {
            tunnel_->stop(2U);
        }
    }
}

void ClientRuntime::run_packet_reader(std::shared_ptr<TlsTunnelClient> tunnel)
{
    try
    {
        while (!stop_requested_)
        {
            auto packet = packet_device_->receive_packet();
            if (!packet.has_value())
            {
                break;
            }
            tunnel->send_ipv4_from_tun(std::move(*packet));
        }
    }
    catch (const std::exception& error)
    {
        record_failure(std::string("virtual adapter read failed: ") + error.what());
        stop_requested_ = true;
        boost::asio::post(io_context_, [tunnel] { tunnel->stop(3U); });
    }
}

void ClientRuntime::on_tunnel_closed(
    const std::optional<protocol::ErrorMessage>& remote_error)
{
    if (remote_error.has_value() && !intentional_stop_)
    {
        record_failure("server rejected the session with error code " +
                       std::to_string(remote_error->code));
    }
    else if (!intentional_stop_ && failure_message().empty())
    {
        record_failure("VPN transport closed unexpectedly");
    }
    finish_on_io();
}

void ClientRuntime::fail_on_io(std::string message)
{
    if (finished_.load())
    {
        return;
    }
    record_failure(message);
    stop_requested_ = true;
    packet_device_->interrupt_receive();
    resolver_.cancel();
    boost::system::error_code ignored;
    if (pending_stream_)
    {
        pending_stream_->lowest_layer().cancel(ignored);
        pending_stream_->lowest_layer().close(ignored);
    }
    if (tunnel_)
    {
        tunnel_->stop(1U);
    }
    else
    {
        finish_on_io();
    }
}

void ClientRuntime::finish_on_io()
{
    if (finished_.exchange(true))
    {
        return;
    }
    stop_requested_ = true;
    packet_device_->interrupt_receive();
    resolver_.cancel();
    static_cast<void>(connection_timer_.cancel());
    if (packet_thread_.joinable() && packet_thread_.get_id() != std::this_thread::get_id())
    {
        packet_thread_.join();
    }
    network_transaction_->rollback();
    pending_stream_.reset();
    tunnel_.reset();
    const auto failure = failure_message();
    if (intentional_stop_ && failure.empty())
    {
        transition(ClientRuntimeState::Stopped, "VPN tunnel stopped");
    }
    else
    {
        transition(ClientRuntimeState::Failed,
                   failure.empty() ? "VPN runtime failed" : failure);
    }
    if (work_guard_.has_value())
    {
        work_guard_->reset();
    }
    io_context_.stop();
}

void ClientRuntime::transition(const ClientRuntimeState state, std::string message)
{
    state_ = state;
    if (event_handler_)
    {
        try
        {
            event_handler_(ClientRuntimeEvent{state, std::move(message)});
        }
        catch (...)
        {
        }
    }
}

void ClientRuntime::record_failure(const std::string& message)
{
    std::lock_guard<std::mutex> lock(failure_mutex_);
    if (failure_message_.empty())
    {
        failure_message_ = message;
    }
}

} // namespace personal_vpn::client
