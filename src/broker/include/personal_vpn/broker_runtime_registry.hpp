#ifndef PERSONAL_VPN_BROKER_RUNTIME_REGISTRY_HPP
#define PERSONAL_VPN_BROKER_RUNTIME_REGISTRY_HPP

#include "personal_vpn/broker_protocol.hpp"
#include "personal_vpn/client_runtime_control.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace personal_vpn::broker
{

class BrokerRuntimeRegistry final
{
   public:
    using RuntimeFactory = std::function<std::unique_ptr<client::ClientRuntimeControl>(
        const std::string&, client::ClientRuntimeControl::EventHandler)>;

    explicit BrokerRuntimeRegistry(RuntimeFactory runtime_factory);
    ~BrokerRuntimeRegistry();

    BrokerRuntimeRegistry(const BrokerRuntimeRegistry&) = delete;
    BrokerRuntimeRegistry& operator=(const BrokerRuntimeRegistry&) = delete;

    // peer_user_sid must come from authenticated named-pipe TokenUser inspection.
    [[nodiscard]] Response dispatch(const std::string& peer_user_sid,
                                    const Request& request);
    void shutdown() noexcept;

   private:
    struct Session;

    RuntimeFactory runtime_factory_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
};

} // namespace personal_vpn::broker

#endif
