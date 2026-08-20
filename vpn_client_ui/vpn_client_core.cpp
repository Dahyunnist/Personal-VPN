#include "vpn_client_core.h"

#include "personal_vpn/client_config.hpp"
#include "personal_vpn/windows_network_backend.hpp"

#include <exception>
#include <utility>

VPNClientCore::~VPNClientCore()
{
    Stop();
}

bool VPNClientCore::Start(const std::string& config_path, LogCallback log_callback)
{
    Stop();
    try
    {
        auto config = personal_vpn::client::load_client_config(config_path);
        auto runtime = std::make_unique<personal_vpn::client::ClientRuntime>(
            std::move(config),
            std::make_unique<personal_vpn::client::WindowsNetworkBackend>(),
            [this](const personal_vpn::client::ClientRuntimeEvent& event)
            { OnRuntimeEvent(event); });
        {
            std::lock_guard<std::mutex> lock(mutex_);
            log_callback_ = std::move(log_callback);
            last_error_.clear();
            runtime_ = std::move(runtime);
        }
        running_ = true;
        runtime_->start();
        return true;
    }
    catch (const std::exception& error)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            log_callback_ = std::move(log_callback);
            last_error_ = error.what();
        }
        connected_ = false;
        running_ = false;
        Log(std::string("启动失败：") + error.what(), true);
        return false;
    }
}

void VPNClientCore::Stop()
{
    std::unique_ptr<personal_vpn::client::ClientRuntime> runtime;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        runtime = std::move(runtime_);
    }
    if (runtime)
    {
        runtime->stop();
        runtime->wait();
    }
    connected_ = false;
    running_ = false;
}

std::string VPNClientCore::LastError() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void VPNClientCore::OnRuntimeEvent(const personal_vpn::client::ClientRuntimeEvent& event)
{
    using personal_vpn::client::ClientRuntimeState;
    connected_ = event.state == ClientRuntimeState::Connected;
    if (event.state == ClientRuntimeState::Stopped || event.state == ClientRuntimeState::Failed)
    {
        running_ = false;
    }
    if (event.state == ClientRuntimeState::Failed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = event.message;
    }
    Log(event.message, event.state == ClientRuntimeState::Failed);
}

void VPNClientCore::Log(const std::string& message, const bool is_error) const
{
    LogCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = log_callback_;
    }
    if (callback && !message.empty())
    {
        callback(message, is_error);
    }
}
