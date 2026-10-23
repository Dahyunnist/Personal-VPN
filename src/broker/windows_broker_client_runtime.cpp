#include "personal_vpn/windows_broker_client_runtime.hpp"

#ifdef _WIN32

#include "personal_vpn/windows_broker_pipe.hpp"

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace personal_vpn::broker
{

BrokerClientRuntimeControl::BrokerClientRuntimeControl(std::string profile_id,
                                                       EventHandler event_handler)
    : profile_id_(std::move(profile_id)), event_handler_(std::move(event_handler))
{
    if (!is_valid_profile_id(profile_id_))
    {
        throw std::invalid_argument("broker profile ID is invalid");
    }
}

BrokerClientRuntimeControl::~BrokerClientRuntimeControl()
{
    stop();
    wait();
}

void BrokerClientRuntimeControl::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_)
    {
        throw std::logic_error("broker client runtime is single-use");
    }
    started_ = true;
    stopping_ = false;
    failure_message_.clear();
    state_ = client::ClientRuntimeState::Starting;
    try
    {
        worker_ = std::thread(&BrokerClientRuntimeControl::run, this);
    }
    catch (...)
    {
        started_ = false;
        state_ = client::ClientRuntimeState::Stopped;
        throw;
    }
}

void BrokerClientRuntimeControl::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_)
        {
            return;
        }
        stopping_ = true;
        if (pipe_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CancelIoEx(pipe_, nullptr));
        }
    }
    wake_.notify_all();
}

void BrokerClientRuntimeControl::wait()
{
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id())
    {
        worker_.join();
    }
}

client::ClientRuntimeState BrokerClientRuntimeControl::state() const noexcept
{
    return state_.load();
}

std::string BrokerClientRuntimeControl::failure_message() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return failure_message_;
}

void BrokerClientRuntimeControl::publish(const client::ClientRuntimeState state,
                                         std::string message)
{
    state_ = state;
    if (state == client::ClientRuntimeState::Failed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failure_message_ = message;
    }
    if (event_handler_)
    {
        try
        {
            event_handler_({state, std::move(message)});
        }
        catch (...)
        {
        }
    }
}

bool BrokerClientRuntimeControl::stopping() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stopping_;
}

void BrokerClientRuntimeControl::close_pipe() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pipe_ != INVALID_HANDLE_VALUE)
    {
        static_cast<void>(CloseHandle(pipe_));
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

void BrokerClientRuntimeControl::run() noexcept
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    try
    {
        const auto connect_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(30);
        while (!stopping())
        {
            pipe = connect_broker_pipe_client(250U);
            if (pipe != INVALID_HANDLE_VALUE)
            {
                break;
            }
            if (std::chrono::steady_clock::now() >= connect_deadline)
            {
                throw std::runtime_error("broker service is unavailable");
            }
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(250), [this] { return stopping_; });
        }
        if (pipe == INVALID_HANDLE_VALUE)
        {
            publish(client::ClientRuntimeState::Stopped);
            return;
        }
        bool cancelled_before_attach = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
            {
                static_cast<void>(CloseHandle(pipe));
                cancelled_before_attach = true;
            }
            else
            {
                pipe_ = pipe;
            }
        }
        if (cancelled_before_attach)
        {
            publish(client::ClientRuntimeState::Stopped);
            return;
        }

        const auto connect_id = next_request_id_++;
        if (!write_broker_request(pipe, {RequestType::Connect, connect_id, profile_id_}))
        {
            throw std::runtime_error("broker Connect request failed");
        }
        const auto connect_response = read_broker_response(pipe, connect_id);
        if (connect_response.type != ResponseType::Accepted)
        {
            throw std::runtime_error(connect_response.message.empty()
                                         ? "broker rejected Connect request"
                                         : connect_response.message);
        }
        publish(client::ClientRuntimeState::Connecting, "已请求 broker 建立 VPN 隧道");

        while (!stopping())
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(500),
                               [this] { return stopping_; });
            }
            if (stopping())
            {
                break;
            }
            const auto status_id = next_request_id_++;
            if (!write_broker_request(pipe, {RequestType::GetStatus, status_id, {}}))
            {
                throw std::runtime_error("broker Status request failed");
            }
            const auto response = read_broker_response(pipe, status_id);
            if (response.type != ResponseType::Status)
            {
                throw std::runtime_error(response.message.empty()
                                             ? "broker returned an invalid Status response"
                                             : response.message);
            }
            const auto next_state = response.connected ? client::ClientRuntimeState::Connected
                                                       : client::ClientRuntimeState::Connecting;
            if (state() != next_state)
            {
                publish(next_state,
                        response.connected ? "VPN 已连接" : "等待服务器分配并配置隧道");
            }
        }

        if (!stopping())
        {
            throw std::runtime_error("broker session ended unexpectedly");
        }
        const auto disconnect_id = next_request_id_++;
        if (write_broker_request(pipe, {RequestType::Disconnect, disconnect_id, {}}))
        {
            static_cast<void>(read_broker_response(pipe, disconnect_id));
        }
        close_pipe();
        publish(client::ClientRuntimeState::Stopped);
    }
    catch (const std::exception& error)
    {
        close_pipe();
        if (stopping())
        {
            publish(client::ClientRuntimeState::Stopped);
        }
        else
        {
            publish(client::ClientRuntimeState::Failed, error.what());
        }
    }
    catch (...)
    {
        close_pipe();
        if (stopping())
        {
            publish(client::ClientRuntimeState::Stopped);
        }
        else
        {
            publish(client::ClientRuntimeState::Failed, "broker client failed unexpectedly");
        }
    }
}

} // namespace personal_vpn::broker

#endif
