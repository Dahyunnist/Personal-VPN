#include "personal_vpn/windows_broker_pipe_session.hpp"

#ifdef _WIN32

#include "personal_vpn/windows_broker_pipe.hpp"

#include <sddl.h>

#include <memory>
#include <stdexcept>
#include <vector>

namespace personal_vpn::broker
{
namespace
{

struct ImpersonationGuard
{
    ~ImpersonationGuard()
    {
        if (active)
        {
            static_cast<void>(RevertToSelf());
        }
    }

    bool revert() noexcept
    {
        if (!active)
        {
            return false;
        }
        active = false;
        return RevertToSelf() != FALSE;
    }

    bool active{true};
};

struct LocalFreeDeleter
{
    void operator()(void* value) const noexcept
    {
        if (value != nullptr)
        {
            static_cast<void>(LocalFree(value));
        }
    }
};

std::string current_user_sid()
{
    HANDLE raw_token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_token))
    {
        throw std::runtime_error("open impersonated broker client token failed");
    }
    std::unique_ptr<void, decltype(&CloseHandle)> token(raw_token, CloseHandle);
    DWORD required = 0U;
    static_cast<void>(GetTokenInformation(raw_token, TokenUser, nullptr, 0U, &required));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0U)
    {
        throw std::runtime_error("query impersonated broker client SID failed");
    }
    std::vector<std::uint8_t> storage(required);
    if (!GetTokenInformation(raw_token, TokenUser, storage.data(), required, &required))
    {
        throw std::runtime_error("read impersonated broker client SID failed");
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    LPWSTR raw_sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &raw_sid))
    {
        throw std::runtime_error("format impersonated broker client SID failed");
    }
    std::unique_ptr<wchar_t, LocalFreeDeleter> sid_owner(raw_sid);
    std::string result;
    for (const auto* character = raw_sid; *character != L'\0'; ++character)
    {
        if (*character > 0x7FU)
        {
            throw std::runtime_error("unexpected non-ASCII SID character");
        }
        result.push_back(static_cast<char>(*character));
    }
    return result;
}

} // namespace

void serve_broker_pipe_session(const HANDLE pipe,
                               const std::wstring& authorized_logon_sid,
                               const DWORD expected_session_id,
                               BrokerRuntimeRegistry& registry) noexcept
{
    RequestDecoder decoder;
    std::string peer_user_sid;
    while (pipe != INVALID_HANDLE_VALUE)
    {
        Request request;
        try
        {
            request = read_authenticated_broker_request(
                pipe, authorized_logon_sid, expected_session_id, decoder);
        }
        catch (...)
        {
            break;
        }
        if (!ImpersonateNamedPipeClient(pipe))
        {
            break;
        }
        ImpersonationGuard impersonation;
        Response response;
        try
        {
            peer_user_sid = current_user_sid();
            response = registry.dispatch(peer_user_sid, request);
        }
        catch (...)
        {
            response = {ResponseType::Rejected,
                        request.request_id,
                        false,
                        "broker request processing failed"};
        }
        if (!impersonation.revert() || !write_broker_response(pipe, response))
        {
            break;
        }
    }
    static_cast<void>(DisconnectNamedPipe(pipe));
    if (!peer_user_sid.empty())
    {
        registry.disconnect_user(peer_user_sid);
    }
}

} // namespace personal_vpn::broker

#endif
