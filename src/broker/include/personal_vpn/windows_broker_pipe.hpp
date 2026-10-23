#ifndef PERSONAL_VPN_WINDOWS_BROKER_PIPE_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_PIPE_HPP

#ifdef _WIN32

#include <windows.h>

#include <string>

namespace personal_vpn::broker
{

// Creates the first instance of the fixed local broker pipe, ACLed to one trusted logon SID.
[[nodiscard]] HANDLE create_broker_pipe_server(const std::wstring& authorized_logon_sid);

} // namespace personal_vpn::broker

#endif
#endif
