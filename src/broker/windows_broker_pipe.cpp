#include "personal_vpn/windows_broker_pipe.hpp"

#ifdef _WIN32

#include <sddl.h>

#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace personal_vpn::broker
{
namespace
{

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

struct HandleCloser
{
    void operator()(void* handle) const noexcept
    {
        if (handle != nullptr)
        {
            static_cast<void>(CloseHandle(handle));
        }
    }
};

[[noreturn]] void fail(const char* operation)
{
    throw std::runtime_error(std::string(operation) + " failed with Win32 error " +
                             std::to_string(GetLastError()));
}

} // namespace

HANDLE create_broker_pipe_server(const std::wstring& authorized_logon_sid)
{
    PSID parsed_sid = nullptr;
    if (authorized_logon_sid.empty() ||
        !ConvertStringSidToSidW(authorized_logon_sid.c_str(), &parsed_sid))
    {
        fail("validate broker logon SID");
    }
    std::unique_ptr<void, LocalFreeDeleter> sid_owner(parsed_sid);
    LPWSTR canonical_sid = nullptr;
    if (!ConvertSidToStringSidW(parsed_sid, &canonical_sid))
    {
        fail("format broker logon SID");
    }
    std::unique_ptr<wchar_t, LocalFreeDeleter> canonical_owner(canonical_sid);

    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;" +
                              std::wstring(canonical_sid) + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
    {
        fail("build broker pipe ACL");
    }
    std::unique_ptr<void, LocalFreeDeleter> descriptor_owner(descriptor);

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    constexpr wchar_t pipe_name[] = L"\\\\.\\pipe\\PersonalVPN-Broker-v1";
    const auto pipe = CreateNamedPipeW(pipe_name,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                           PIPE_REJECT_REMOTE_CLIENTS,
                                       1U,
                                       256U,
                                       256U,
                                       0U,
                                       &attributes);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        fail("create secure broker pipe");
    }
    return pipe;
}

bool authenticate_broker_pipe_client(const HANDLE pipe,
                                     const std::wstring& authorized_logon_sid,
                                     const DWORD expected_session_id)
{
    DWORD pipe_session_id = 0U;
    if (pipe == INVALID_HANDLE_VALUE ||
        !GetNamedPipeClientSessionId(pipe, &pipe_session_id) ||
        pipe_session_id != expected_session_id || !ImpersonateNamedPipeClient(pipe))
    {
        return false;
    }
    ImpersonationGuard impersonation_guard;

    bool authenticated = false;
    HANDLE token = nullptr;
    std::unique_ptr<void, HandleCloser> token_owner;
    PSID expected_sid = nullptr;
    do
    {
        if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        {
            break;
        }
        token_owner.reset(token);
        if (!ConvertStringSidToSidW(authorized_logon_sid.c_str(), &expected_sid))
        {
            break;
        }
        DWORD session_id = 0U;
        DWORD returned = 0U;
        if (!GetTokenInformation(token, TokenSessionId, &session_id, sizeof(session_id), &returned) ||
            session_id != expected_session_id)
        {
            break;
        }
        DWORD groups_size = 0U;
        static_cast<void>(GetTokenInformation(token, TokenGroups, nullptr, 0U, &groups_size));
        if (groups_size == 0U)
        {
            break;
        }
        std::vector<std::uint8_t> groups_buffer(groups_size);
        if (!GetTokenInformation(token,
                                 TokenGroups,
                                 groups_buffer.data(),
                                 groups_size,
                                 &returned))
        {
            break;
        }
        const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(groups_buffer.data());
        for (DWORD index = 0U; index < groups->GroupCount; ++index)
        {
            const auto& group = groups->Groups[index];
            if ((group.Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID &&
                EqualSid(group.Sid, expected_sid))
            {
                authenticated = true;
                break;
            }
        }
    } while (false);

    if (expected_sid != nullptr)
    {
        static_cast<void>(LocalFree(expected_sid));
    }
    const auto reverted = impersonation_guard.revert();
    return authenticated && reverted;
}

} // namespace personal_vpn::broker

#endif
