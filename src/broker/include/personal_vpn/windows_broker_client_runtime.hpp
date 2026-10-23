#ifndef PERSONAL_VPN_WINDOWS_BROKER_CLIENT_RUNTIME_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_CLIENT_RUNTIME_HPP

#ifdef _WIN32

#include "personal_vpn/client_runtime_control.hpp"

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace personal_vpn::broker
{

class BrokerClientRuntimeControl final : public client::ClientRuntimeControl
{
   public:
    BrokerClientRuntimeControl(std::string profile_id, EventHandler event_handler);
    ~BrokerClientRuntimeControl() override;

    BrokerClientRuntimeControl(const BrokerClientRuntimeControl&) = delete;
    BrokerClientRuntimeControl& operator=(const BrokerClientRuntimeControl&) = delete;

    void start() override;
    void stop() override;
    void wait() override;
    [[nodiscard]] client::ClientRuntimeState state() const noexcept override;
    [[nodiscard]] std::string failure_message() const override;

   private:
    void run() noexcept;
    void publish(client::ClientRuntimeState state, std::string message = {});
    [[nodiscard]] bool stopping() const noexcept;
    void close_pipe() noexcept;

    const std::string profile_id_;
    const EventHandler event_handler_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    HANDLE pipe_{INVALID_HANDLE_VALUE};
    std::uint64_t next_request_id_{1U};
    bool started_{false};
    bool stopping_{false};
    std::atomic<client::ClientRuntimeState> state_{client::ClientRuntimeState::Stopped};
    std::string failure_message_;
};

} // namespace personal_vpn::broker

#endif
#endif
