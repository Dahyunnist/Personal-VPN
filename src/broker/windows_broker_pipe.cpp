#include "personal_vpn/windows_broker_pipe.hpp"

#ifdef _WIN32

#include <sddl.h>

#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>

namespace personal_vpn::broker
{
namespace
{

constexpr wchar_t broker_pipe_name[] = L"\\\\.\\pipe\\PersonalVPN-Broker-v1";

constexpr DWORD pipe_operation_timeout_ms = 15'000U;

struct EventCloser
{
    void operator()(void* event) const noexcept
    {
        if (event != nullptr)
        {
            static_cast<void>(CloseHandle(event));
        }
    }
};

bool wait_overlapped(HANDLE pipe, OVERLAPPED& operation, const DWORD timeout_ms) noexcept
{
    const auto wait_result = WaitForSingleObject(operation.hEvent, timeout_ms);
    if (wait_result != WAIT_OBJECT_0)
    {
        static_cast<void>(CancelIoEx(pipe, &operation));
        DWORD ignored = 0U;
        static_cast<void>(GetOverlappedResult(pipe, &operation, &ignored, TRUE));
        return false;
    }
    DWORD ignored = 0U;
    return GetOverlappedResult(pipe, &operation, &ignored, FALSE) != FALSE;
}

bool read_exact_pipe(HANDLE pipe,
                     std::uint8_t* output,
                     const std::size_t size,
                     const ULONGLONG deadline) noexcept
{
    std::size_t offset = 0U;
    while (offset < size)
    {
        const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr)
        {
            return false;
        }
        std::unique_ptr<void, EventCloser> event_owner(event);
        OVERLAPPED operation{};
        operation.hEvent = event;
        DWORD transferred = 0U;
        const auto remaining = static_cast<DWORD>(size - offset);
        const auto started = ReadFile(pipe, output + offset, remaining, &transferred, &operation);
        if (!started && GetLastError() != ERROR_IO_PENDING)
        {
            return false;
        }
        if (!started)
        {
            const auto now = GetTickCount64();
            if (now >= deadline || !wait_overlapped(
                                       pipe,
                                       operation,
                                       static_cast<DWORD>(deadline - now)))
            {
                return false;
            }
            if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
            {
                return false;
            }
        }
        if (transferred == 0U)
        {
            return false;
        }
        offset += transferred;
    }
    return true;
}

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

HANDLE connect_broker_pipe_client(const DWORD timeout_ms) noexcept
{
    if (timeout_ms == 0U || !WaitNamedPipeW(broker_pipe_name, timeout_ms))
    {
        return INVALID_HANDLE_VALUE;
    }
    return CreateFileW(broker_pipe_name,
                       GENERIC_READ | GENERIC_WRITE,
                       0U,
                       nullptr,
                       OPEN_EXISTING,
                       SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION,
                       nullptr);
}

bool write_broker_request(const HANDLE pipe, const Request& request)
{
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const auto frame = encode_request(request);
    std::size_t offset = 0U;
    while (offset < frame.size())
    {
        DWORD written = 0U;
        const auto remaining = static_cast<DWORD>(frame.size() - offset);
        if (!WriteFile(pipe, frame.data() + offset, remaining, &written, nullptr) || written == 0U)
        {
            return false;
        }
        offset += written;
    }
    return true;
}

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
    const auto pipe = CreateNamedPipeW(broker_pipe_name,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
                                           FILE_FLAG_OVERLAPPED,
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

bool accept_broker_pipe_client(const HANDLE pipe) noexcept
{
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr)
    {
        return false;
    }
    std::unique_ptr<void, EventCloser> event_owner(event);
    OVERLAPPED operation{};
    operation.hEvent = event;
    if (ConnectNamedPipe(pipe, &operation) != FALSE)
    {
        return true;
    }
    const auto error = GetLastError();
    if (error == ERROR_PIPE_CONNECTED)
    {
        return true;
    }
    return error == ERROR_IO_PENDING &&
           wait_overlapped(pipe, operation, pipe_operation_timeout_ms);
}

bool authenticate_broker_pipe_client(const HANDLE pipe,
                                     const std::wstring& authorized_logon_sid,
                                     const DWORD expected_session_id)
{
    DWORD pipe_session_id = 0U;
    if (pipe == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }
    if (!GetNamedPipeClientSessionId(pipe, &pipe_session_id))
    {
        return false;
    }
    if (pipe_session_id != expected_session_id)
    {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    if (!ImpersonateNamedPipeClient(pipe))
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
        SECURITY_IMPERSONATION_LEVEL impersonation_level = SecurityAnonymous;
        if (!GetTokenInformation(token,
                                 TokenImpersonationLevel,
                                 &impersonation_level,
                                 sizeof(impersonation_level),
                                 &returned) ||
            impersonation_level < SecurityImpersonation)
        {
            break;
        }
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
    if (!authenticated || !reverted)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }
    return true;
}

Request read_authenticated_broker_request(const HANDLE pipe,
                                          const std::wstring& authorized_logon_sid,
                                          const DWORD expected_session_id,
                                          RequestDecoder& decoder)
{
    const auto disconnect_and_throw = [pipe](const char* message) -> void
    {
        static_cast<void>(DisconnectNamedPipe(pipe));
        throw std::runtime_error(message);
    };
    if (!authenticate_broker_pipe_client(pipe, authorized_logon_sid, expected_session_id))
    {
        static_cast<void>(DisconnectNamedPipe(pipe));
        throw std::runtime_error("broker pipe peer authentication failed with Win32 error " +
                                 std::to_string(GetLastError()));
    }

    const auto deadline = GetTickCount64() + pipe_operation_timeout_ms;
    std::vector<std::uint8_t> frame(4U);
    if (!read_exact_pipe(pipe, frame.data(), frame.size(), deadline))
    {
        disconnect_and_throw("broker frame length read failed");
    }
    const auto body_size = static_cast<std::uint32_t>(frame[0]) |
                           (static_cast<std::uint32_t>(frame[1]) << 8U) |
                           (static_cast<std::uint32_t>(frame[2]) << 16U) |
                           (static_cast<std::uint32_t>(frame[3]) << 24U);
    if (body_size < 16U || body_size > maximum_frame_size - 4U)
    {
        disconnect_and_throw("broker frame length is outside the allowed range");
    }
    frame.resize(static_cast<std::size_t>(body_size) + 4U);
    if (!read_exact_pipe(pipe,
                         frame.data() + 4U,
                         frame.size() - 4U,
                         deadline))
    {
        disconnect_and_throw("broker frame body read failed or timed out");
    }
    try
    {
        return decoder.decode(frame);
    }
    catch (...)
    {
        static_cast<void>(DisconnectNamedPipe(pipe));
        throw;
    }
}

} // namespace personal_vpn::broker

#endif
