#include "windows_kill_switch_policy.hpp"

#include <algorithm>
#include <stdexcept>

namespace personal_vpn::client::detail
{
namespace
{

constexpr std::size_t kMaximumEndpoints = 32U;

bool is_valid_endpoint(const WindowsVpnEndpoint& endpoint)
{
    const auto address_length = endpoint.ipv6 ? 16U : 4U;
    const bool all_zero = std::all_of(endpoint.address.begin(),
                                      endpoint.address.begin() + address_length,
                                      [](const std::uint8_t octet) { return octet == 0U; });
    return endpoint.port != 0U && !all_zero;
}

} // namespace

std::array<KillSwitchFamilyPolicy, 2U> build_kill_switch_policy(
    const std::vector<WindowsVpnEndpoint>& endpoints)
{
    if (endpoints.empty() || endpoints.size() > kMaximumEndpoints)
    {
        throw std::invalid_argument("kill switch requires valid VPN endpoints and interface");
    }
    if (!std::all_of(endpoints.begin(), endpoints.end(), is_valid_endpoint))
    {
        throw std::invalid_argument("kill switch endpoint address or port is invalid");
    }

    std::array<KillSwitchFamilyPolicy, 2U> policies{{
        {IpFamily::IPv4, {}, true, true, true},
        {IpFamily::IPv6, {}, true, true, true},
    }};
    for (const auto& endpoint : endpoints)
    {
        policies[endpoint.ipv6 ? 1U : 0U].endpoints.push_back(endpoint);
    }
    return policies;
}

TunnelPermitPolicy build_tunnel_permit_policy(const std::uint32_t interface_index)
{
    if (interface_index == 0U)
    {
        throw std::invalid_argument("Wintun interface index is invalid");
    }
    return {interface_index, {IpFamily::IPv4, IpFamily::IPv6}};
}

} // namespace personal_vpn::client::detail
