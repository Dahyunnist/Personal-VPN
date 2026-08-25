#include "personal_vpn/tunnel_router.hpp"

#include <utility>

namespace personal_vpn::core
{

bool TunnelRouter::register_peer(const Ipv4Address& address,
                                 const std::shared_ptr<TunnelPeer>& peer)
{
    if (!peer)
    {
        return false;
    }
    const auto key = address_key(address);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = peers_.find(key);
    if (existing != peers_.end())
    {
        const auto active = existing->second.lock();
        if (active)
        {
            return active.get() == peer.get();
        }
        peers_.erase(existing);
    }
    peers_.emplace(key, peer);
    return true;
}

bool TunnelRouter::unregister_peer(const Ipv4Address& address,
                                   const TunnelPeer* expected_peer)
{
    if (expected_peer == nullptr)
    {
        return false;
    }
    const auto key = address_key(address);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = peers_.find(key);
    if (existing == peers_.end())
    {
        return false;
    }
    const auto active = existing->second.lock();
    if (active && active.get() != expected_peer)
    {
        return false;
    }
    peers_.erase(existing);
    return true;
}

std::shared_ptr<TunnelPeer> TunnelRouter::find_peer(const Ipv4Address& address)
{
    const auto key = address_key(address);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = peers_.find(key);
    if (existing == peers_.end())
    {
        return {};
    }
    auto peer = existing->second.lock();
    if (!peer)
    {
        peers_.erase(existing);
    }
    return peer;
}

bool TunnelRouter::route_ipv4_packet(std::vector<std::uint8_t> packet)
{
    if (packet.size() < 20U)
    {
        return false;
    }
    const auto version = static_cast<std::uint8_t>(packet[0] >> 4U);
    const auto header_size = static_cast<std::size_t>(packet[0] & 0x0FU) * 4U;
    const auto total_size = static_cast<std::size_t>(
        (static_cast<std::uint16_t>(packet[2]) << 8U) |
        static_cast<std::uint16_t>(packet[3]));
    if (version != 4U || header_size < 20U || header_size > packet.size() ||
        total_size != packet.size())
    {
        return false;
    }
    const Ipv4Address destination{packet[16], packet[17], packet[18], packet[19]};
    auto peer = find_peer(destination);
    if (!peer)
    {
        return false;
    }
    peer->send_ipv4_from_tun(std::move(packet));
    return true;
}

std::size_t TunnelRouter::active_routes()
{
    std::lock_guard<std::mutex> lock(mutex_);
    remove_expired_locked();
    return peers_.size();
}

std::uint32_t TunnelRouter::address_key(const Ipv4Address& address) noexcept
{
    return (static_cast<std::uint32_t>(address[0]) << 24U) |
           (static_cast<std::uint32_t>(address[1]) << 16U) |
           (static_cast<std::uint32_t>(address[2]) << 8U) |
           static_cast<std::uint32_t>(address[3]);
}

void TunnelRouter::remove_expired_locked()
{
    for (auto current = peers_.begin(); current != peers_.end();)
    {
        if (current->second.expired())
        {
            current = peers_.erase(current);
        }
        else
        {
            ++current;
        }
    }
}

} // namespace personal_vpn::core
