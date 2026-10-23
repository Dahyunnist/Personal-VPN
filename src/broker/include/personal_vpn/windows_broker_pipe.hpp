#ifndef PERSONAL_VPN_WINDOWS_BROKER_PIPE_HPP
#define PERSONAL_VPN_WINDOWS_BROKER_PIPE_HPP

#ifdef _WIN32

#include <windows.h>

#include <string>
#include <vector>

#include "personal_vpn/broker_protocol.hpp"

namespace personal_vpn::broker
{

// Creates the first instance of the fixed local broker pipe, ACLed to one trusted logon SID.
[[nodiscard]] HANDLE create_broker_pipe_server(const std::wstring& authorized_logon_sid);
[[nodiscard]] bool accept_broker_pipe_client(HANDLE pipe) noexcept;

// Impersonates the connected peer and requires the ACL-authorized logon SID and session ID.
[[nodiscard]] bool authenticate_broker_pipe_client(HANDLE pipe,
                                                    const std::wstring& authorized_logon_sid,
                                                    DWORD expected_session_id);

// Reads one bounded frame, authenticates its peer, and rejects replays before returning it.
[[nodiscard]] Request read_authenticated_broker_request(
    HANDLE pipe,
    const std::wstring& authorized_logon_sid,
    DWORD expected_session_id,
    RequestDecoder& decoder);

} // namespace personal_vpn::broker

#endif
#endif
