#ifndef PERSONAL_VPN_WINDOWS_KILL_SWITCH_HPP
#define PERSONAL_VPN_WINDOWS_KILL_SWITCH_HPP

#include <array>
#include <cstdint>
#include <vector>

#include "personal_vpn/windows_network_backend.hpp"

namespace personal_vpn::client
{

void enable_windows_kill_switch(const std::vector<WindowsVpnEndpoint>& endpoints);
void replace_windows_tunnel_permit(void*& dynamic_session, std::uint32_t tunnel_interface_index);
std::uint32_t close_windows_tunnel_permit(void*& dynamic_session) noexcept;
void disable_windows_kill_switch();

} // namespace personal_vpn::client

#endif
