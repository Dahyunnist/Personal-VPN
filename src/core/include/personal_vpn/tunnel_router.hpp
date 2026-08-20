#ifndef PERSONAL_VPN_TUNNEL_ROUTER_HPP
#define PERSONAL_VPN_TUNNEL_ROUTER_HPP

#include "personal_vpn/lease_manager.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace personal_vpn::core
{

class TunnelPeer
{
   public:
    virtual ~TunnelPeer() = default;
    virtual void send_ipv4_from_tun(std::vector<std::uint8_t> packet) = 0;
};

class TunnelRouter
{
   public:
    [[nodiscard]] bool register_peer(const Ipv4Address& address,
                                     const std::shared_ptr<TunnelPeer>& peer);
    [[nodiscard]] bool unregister_peer(const Ipv4Address& address,
                                       const TunnelPeer* expected_peer);
    [[nodiscard]] std::shared_ptr<TunnelPeer> find_peer(const Ipv4Address& address);
    [[nodiscard]] bool route_ipv4_packet(std::vector<std::uint8_t> packet);
    [[nodiscard]] std::size_t active_routes();

   private:
    [[nodiscard]] static std::uint32_t address_key(const Ipv4Address& address) noexcept;
    void remove_expired_locked();

    std::mutex mutex_;
    std::unordered_map<std::uint32_t, std::weak_ptr<TunnelPeer>> peers_;
};

} // namespace personal_vpn::core

#endif
