#ifndef PERSONAL_VPN_CLIENT_RUNTIME_CONTROL_HPP
#define PERSONAL_VPN_CLIENT_RUNTIME_CONTROL_HPP

#include <functional>
#include <string>

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
    Reconnecting,
    Stopping,
    Failed,
};

struct ClientRuntimeEvent
{
    ClientRuntimeState state{ClientRuntimeState::Stopped};
    std::string message;
};

class ClientRuntimeControl
{
   public:
    using EventHandler = std::function<void(const ClientRuntimeEvent&)>;

    virtual ~ClientRuntimeControl() = default;

    ClientRuntimeControl(const ClientRuntimeControl&) = delete;
    ClientRuntimeControl& operator=(const ClientRuntimeControl&) = delete;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void wait() = 0;
    [[nodiscard]] virtual ClientRuntimeState state() const noexcept = 0;
    [[nodiscard]] virtual std::string failure_message() const = 0;

   protected:
    ClientRuntimeControl() = default;
};

} // namespace personal_vpn::client

#endif
