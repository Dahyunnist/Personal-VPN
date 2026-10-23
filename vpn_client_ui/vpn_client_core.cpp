#include "vpn_client_core.h"

#include "personal_vpn/windows_broker_client_runtime.hpp"

#include <array>
#include <exception>
#include <system_error>
#include <utility>

#include <Windows.h>
#include <shellapi.h>

VPNClientCore::~VPNClientCore()
{
    Stop();
}

bool VPNClientCore::Start(std::string profile_id, LogCallback log_callback)
{
    Stop();
    try
    {
        auto runtime = std::make_unique<personal_vpn::broker::BrokerClientRuntimeControl>(
            std::move(profile_id),
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
        Stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            log_callback_ = std::move(log_callback);
            last_error_ = error.what();
        }
        connected_ = false;
        reconnecting_ = false;
        running_ = false;
        Log(std::string("启动失败：") + error.what(), true);
        return false;
    }
}

void VPNClientCore::Stop()
{
    std::unique_ptr<personal_vpn::client::ClientRuntimeControl> runtime;
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
    reconnecting_ = false;
    running_ = false;
}

void VPNClientCore::DisableKillSwitch()
{
    std::array<wchar_t, 32'768U> executable_path{};
    const auto length = GetModuleFileNameW(nullptr,
                                           executable_path.data(),
                                           static_cast<DWORD>(executable_path.size()));
    if (length == 0U || length >= executable_path.size())
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "resolve PersonalVPN executable path");
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"runas", executable_path.data(), L"--disable-kill-switch", nullptr,
        SW_SHOWNORMAL));
    if (result <= 32)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "start elevated kill-switch recovery helper");
    }
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
    reconnecting_ = event.state == ClientRuntimeState::Reconnecting;
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
