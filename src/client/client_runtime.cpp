#include "personal_vpn/client_runtime.hpp"

#if defined(_WIN32)
#include "personal_vpn/windows_network_backend.hpp"
#endif

#include <boost/asio/connect.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace personal_vpn::client
{
namespace
{

bool requests_full_tunnel(const ClientConfig& config)
{
    return config.allow_default_route &&
           std::find(config.routes.begin(), config.routes.end(), "0.0.0.0/0") !=
               config.routes.end();
}

} // namespace

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
      connection_timer_(io_context_),
      reconnect_timer_(io_context_),
      reconnect_random_(std::random_device{}())
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
            static_cast<void>(reconnect_timer_.cancel());
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
    const auto generation = ++connection_generation_;
    transition(ClientRuntimeState::Resolving, "resolving VPN server");
    connection_timer_.expires_after(connection_timeout_);
    connection_timer_.async_wait(
        [this, generation](const boost::system::error_code& error)
        {
            if (!error && generation == connection_generation_ && !finished_.load())
            {
                retry_on_io("connection timed out before tunnel establishment");
            }
        });
    resolver_.async_resolve(
        config_.server_host,
        std::to_string(config_.server_port),
        [this, generation](const boost::system::error_code& error,
               const Tcp::resolver::results_type& endpoints)
        { handle_resolve(generation, error, endpoints); });
}

void ClientRuntime::handle_resolve(const std::uint64_t generation,
                                   const boost::system::error_code& error,
                                   const Tcp::resolver::results_type& endpoints)
{
    if (generation != connection_generation_)
    {
        return;
    }
    if (stop_requested_)
    {
        finish_on_io();
        return;
    }
    if (error)
    {
        retry_on_io("server name resolution failed: " + error.message());
        return;
    }
    if (requests_full_tunnel(config_))
    {
#if defined(_WIN32)
        auto* windows_backend = dynamic_cast<WindowsNetworkBackend*>(packet_device_.get());
        if (windows_backend == nullptr)
        {
            record_failure("full-tunnel mode requires the Windows fail-closed network backend");
            finish_on_io();
            return;
        }
        try
        {
            std::vector<WindowsVpnEndpoint> vpn_endpoints;
            for (const auto& endpoint : endpoints)
            {
                WindowsVpnEndpoint vpn_endpoint;
                vpn_endpoint.port = endpoint.endpoint().port();
                if (endpoint.endpoint().address().is_v4())
                {
                    const auto address = endpoint.endpoint().address().to_v4().to_bytes();
                    std::copy(address.begin(), address.end(), vpn_endpoint.address.begin());
                }
                else
                {
                    const auto address = endpoint.endpoint().address().to_v6().to_bytes();
                    std::copy(address.begin(), address.end(), vpn_endpoint.address.begin());
                    vpn_endpoint.ipv6 = true;
                }
                vpn_endpoints.push_back(vpn_endpoint);
            }
            windows_backend->enable_kill_switch(vpn_endpoints);
        }
        catch (const std::exception& failure)
        {
            record_failure(std::string("failed to enable persistent dual-stack kill switch: ") +
                           failure.what());
            finish_on_io();
            return;
        }
#else
        record_failure("full-tunnel mode is unavailable without a dual-stack kill switch");
        finish_on_io();
        return;
#endif
    }
    transition(ClientRuntimeState::Connecting, "connecting with mutual TLS");
    pending_stream_ =
        std::make_unique<TlsTunnelClient::TlsStream>(io_context_, tls_context_);
    boost::asio::async_connect(
        pending_stream_->lowest_layer(),
        endpoints,
        [this, generation](const boost::system::error_code& connect_error,
                           const Tcp::endpoint&)
        { handle_connect(generation, connect_error); });
}

void ClientRuntime::handle_connect(const std::uint64_t generation,
                                   const boost::system::error_code& error)
{
    if (generation != connection_generation_)
    {
        return;
    }
    if (stop_requested_)
    {
        finish_on_io();
        return;
    }
    if (error)
    {
        retry_on_io("TCP connection failed: " + error.message());
        return;
    }
    tunnel_ = std::make_shared<TlsTunnelClient>(
        std::move(*pending_stream_),
        config_.expected_server_name,
        [this, generation](const protocol::IpAssignment& assignment)
        {
            return generation == connection_generation_ && configure_network(assignment);
        },
        [this, generation](const std::vector<std::uint8_t>& packet)
        {
            if (generation == connection_generation_)
            {
                deliver_to_packet_device(packet);
            }
        },
        [this, generation](const std::optional<protocol::ErrorMessage>& remote_error,
                           const bool retryable)
        { on_tunnel_closed(generation, remote_error, retryable); },
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
    reconnect_attempt_ = 0U;
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

void ClientRuntime::retry_on_io(std::string reason)
{
    if (finished_.load())
    {
        return;
    }
    if (stop_requested_ || intentional_stop_)
    {
        finish_on_io();
        return;
    }

    ++connection_generation_;
    resolver_.cancel();
    static_cast<void>(connection_timer_.cancel());
    if (pending_stream_)
    {
        boost::system::error_code ignored;
        pending_stream_->lowest_layer().cancel(ignored);
        pending_stream_->lowest_layer().close(ignored);
        pending_stream_.reset();
    }
    if (tunnel_)
    {
        tunnel_->abort();
    }

    packet_device_->interrupt_receive();
    if (packet_thread_.joinable() && packet_thread_.get_id() != std::this_thread::get_id())
    {
        packet_thread_.join();
    }
    network_transaction_->rollback();
    tunnel_.reset();

    const auto delay = next_reconnect_delay();
    transition(ClientRuntimeState::Reconnecting,
               std::move(reason) + "; retrying in " + std::to_string(delay.count()) + "ms");
    reconnect_timer_.expires_after(delay);
    reconnect_timer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            if (!error && !stop_requested_ && !finished_.load())
            {
                begin_connect();
            }
            else if (stop_requested_)
            {
                finish_on_io();
            }
        });
}

std::chrono::milliseconds ClientRuntime::next_reconnect_delay()
{
    constexpr std::uint32_t base_delay_ms = 1'000U;
    constexpr std::uint32_t maximum_delay_ms = 30'000U;
    constexpr std::size_t maximum_exponent = 5U;
    const auto exponent = std::min(reconnect_attempt_, maximum_exponent);
    const auto exponential = base_delay_ms << exponent;
    const auto cap_ms = std::min(exponential, maximum_delay_ms);
    if (reconnect_attempt_ < maximum_exponent)
    {
        ++reconnect_attempt_;
    }
    std::uniform_int_distribution<std::uint32_t> jitter(cap_ms / 2U, cap_ms);
    return std::chrono::milliseconds(jitter(reconnect_random_));
}

void ClientRuntime::on_tunnel_closed(
    const std::uint64_t generation,
    const std::optional<protocol::ErrorMessage>& remote_error,
    const bool retryable)
{
    if (generation != connection_generation_)
    {
        return;
    }
    if (intentional_stop_ || stop_requested_)
    {
        finish_on_io();
        return;
    }
    if (remote_error.has_value())
    {
        record_failure("server rejected the session with error code " +
                       std::to_string(remote_error->code));
    }
    else if (!retryable)
    {
        record_failure("VPN tunnel closed because of a non-retryable TLS or protocol error");
    }
    if (!failure_message().empty())
    {
        finish_on_io();
        return;
    }
    retry_on_io("VPN transport closed unexpectedly");
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
    static_cast<void>(reconnect_timer_.cancel());
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
