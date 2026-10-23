#ifndef PERSONAL_VPN_WINDOWS_KILL_SWITCH_POLICY_HPP
#define PERSONAL_VPN_WINDOWS_KILL_SWITCH_POLICY_HPP

#include "personal_vpn/windows_network_backend.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace personal_vpn::client::detail
{

enum class IpFamily
{
    IPv4,
    IPv6
};

struct KillSwitchFamilyPolicy
{
    IpFamily family;
    std::vector<WindowsVpnEndpoint> endpoints;
    bool boot_time_ip_block;
    bool persistent_transport_block;
    bool persistent_ip_block;
};

struct TunnelPermitPolicy
{
    std::uint32_t interface_index;
    std::array<IpFamily, 2U> families;
};

[[nodiscard]] std::array<KillSwitchFamilyPolicy, 2U> build_kill_switch_policy(
    const std::vector<WindowsVpnEndpoint>& endpoints);
[[nodiscard]] TunnelPermitPolicy build_tunnel_permit_policy(std::uint32_t interface_index);

} // namespace personal_vpn::client::detail

#endif
