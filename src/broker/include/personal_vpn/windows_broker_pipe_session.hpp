#ifndef PERSONAL_VPN_WINDOWS_BROKER_PIPE_SESSION_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_PIPE_SESSION_HPP

#ifdef _WIN32

#include <windows.h>

#include <string>

#include "personal_vpn/broker_runtime_registry.hpp"

namespace personal_vpn::broker
{

// Serves requests on one authenticated pipe connection until peer disconnect or failure.
void serve_broker_pipe_session(HANDLE pipe,
                              const std::wstring& authorized_logon_sid,
                              DWORD expected_session_id,
                              BrokerRuntimeRegistry& registry) noexcept;

} // namespace personal_vpn::broker

#endif
#endif
