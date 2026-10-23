#ifndef VPN_CLIENT_CORE_H
#define VPN_CLIENT_CORE_H

#include "personal_vpn/client_runtime_control.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class VPNClientCore final
{
   public:
    using LogCallback = std::function<void(const std::string&, bool)>;

    VPNClientCore() = default;
    ~VPNClientCore();

    VPNClientCore(const VPNClientCore&) = delete;
    VPNClientCore& operator=(const VPNClientCore&) = delete;

    bool Start(std::string profile_id, LogCallback log_callback = {});
    void Stop();
    static void DisableKillSwitch();

    [[nodiscard]] bool IsConnected() const noexcept { return connected_.load(); }
    [[nodiscard]] bool IsReconnecting() const noexcept { return reconnecting_.load(); }
    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }
    [[nodiscard]] std::string LastError() const;

   private:
    void OnRuntimeEvent(const personal_vpn::client::ClientRuntimeEvent& event);
    void Log(const std::string& message, bool is_error) const;

    mutable std::mutex mutex_;
    std::unique_ptr<personal_vpn::client::ClientRuntimeControl> runtime_;
    LogCallback log_callback_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> reconnecting_{false};
    std::atomic<bool> running_{false};
    std::string last_error_;
};

#endif
