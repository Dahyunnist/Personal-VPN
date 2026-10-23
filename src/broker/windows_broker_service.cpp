#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WtsApi32.h>
#include <ShlObj.h>
#include <sddl.h>

#include "personal_vpn/broker_runtime_registry.hpp"
#include "personal_vpn/broker_service_policy.hpp"
#include "personal_vpn/windows_broker_pipe.hpp"
#include "personal_vpn/windows_broker_pipe_session.hpp"
#include "personal_vpn/windows_broker_profile_loader.hpp"
#include "personal_vpn/client_runtime.hpp"
#include "personal_vpn/windows_network_backend.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace
{

constexpr wchar_t service_name[] = L"PersonalVPNBroker";
constexpr DWORD no_active_session = 0xFFFFFFFFU;

SERVICE_STATUS_HANDLE service_status_handle = nullptr;
SERVICE_STATUS service_status{};
HANDLE stop_event = nullptr;
std::mutex pipe_mutex;
HANDLE active_pipe = INVALID_HANDLE_VALUE;
std::atomic<DWORD> active_session_id{no_active_session};

struct HandleCloser
{
    void operator()(void* value) const noexcept
    {
        if (value != nullptr && value != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value));
        }
    }
};

void report_status(const DWORD state,
                   const DWORD error = NO_ERROR,
                   const DWORD wait_hint = 0U)
{
    service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwCurrentState = state;
    service_status.dwWin32ExitCode = error;
    service_status.dwWaitHint = wait_hint;
    service_status.dwCheckPoint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING
                                      ? service_status.dwCheckPoint + 1U
                                      : 0U;
    service_status.dwControlsAccepted = state == SERVICE_RUNNING
                                            ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN |
                                                  SERVICE_ACCEPT_SESSIONCHANGE
                                            : 0U;
    if (service_status_handle != nullptr)
    {
        static_cast<void>(SetServiceStatus(service_status_handle, &service_status));
    }
}

void cancel_active_pipe() noexcept
{
    std::lock_guard<std::mutex> lock(pipe_mutex);
    if (active_pipe != INVALID_HANDLE_VALUE)
    {
        static_cast<void>(CancelIoEx(active_pipe, nullptr));
    }
}

DWORD WINAPI service_control_handler(const DWORD control,
                                     const DWORD event_type,
                                     void* event_data,
                                     void*)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
    {
        report_status(SERVICE_STOP_PENDING, NO_ERROR, 20'000U);
        if (stop_event != nullptr)
        {
            static_cast<void>(SetEvent(stop_event));
        }
        cancel_active_pipe();
        return NO_ERROR;
    }
    if (control == SERVICE_CONTROL_SESSIONCHANGE)
    {
        const auto* notification = static_cast<const WTSSESSION_NOTIFICATION*>(event_data);
        const bool leaving = event_type == WTS_SESSION_LOGOFF ||
                             event_type == WTS_CONSOLE_DISCONNECT ||
                             event_type == WTS_REMOTE_DISCONNECT;
        if (leaving && notification != nullptr &&
            notification->dwSessionId == active_session_id.load())
        {
            cancel_active_pipe();
        }
        return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

std::wstring current_logon_sid(HANDLE token)
{
    DWORD required = 0U;
    static_cast<void>(GetTokenInformation(token, TokenGroups, nullptr, 0U, &required));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required == 0U)
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "query console logon SID size");
    }
    std::vector<std::uint8_t> storage(required);
    if (!GetTokenInformation(token, TokenGroups, storage.data(), required, &required))
    {
        throw std::system_error(static_cast<int>(GetLastError()),
                                std::system_category(),
                                "read console logon SID");
    }
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(storage.data());
    for (DWORD index = 0U; index < groups->GroupCount; ++index)
    {
        const auto& group = groups->Groups[index];
        if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
        {
            continue;
        }
        LPWSTR raw_sid = nullptr;
        if (!ConvertSidToStringSidW(group.Sid, &raw_sid))
        {
            throw std::system_error(static_cast<int>(GetLastError()),
                                    std::system_category(),
                                    "format console logon SID");
        }
        std::unique_ptr<wchar_t, decltype(&LocalFree)> sid_owner(raw_sid, LocalFree);
        return raw_sid;
    }
    throw std::invalid_argument("active console token has no logon SID");
}

std::filesystem::path service_policy_path()
{
    PWSTR raw_program_data = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr,
                                             &raw_program_data);
    if (FAILED(result) || raw_program_data == nullptr)
    {
        throw std::system_error(static_cast<int>(result),
                                std::system_category(),
                                "resolve ProgramData directory");
    }
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> directory_owner(
        raw_program_data, CoTaskMemFree);
    return std::filesystem::path(raw_program_data) / L"PersonalVPN" / L"broker-policy.json";
}

void wait_for_stop(const DWORD milliseconds)
{
    static_cast<void>(WaitForSingleObject(stop_event, milliseconds));
}

void close_active_pipe(const HANDLE pipe) noexcept
{
    std::lock_guard<std::mutex> lock(pipe_mutex);
    if (active_pipe == pipe)
    {
        active_pipe = INVALID_HANDLE_VALUE;
    }
    static_cast<void>(CloseHandle(pipe));
}

void run_broker_service()
{
    const auto policy = personal_vpn::broker::load_broker_network_policy(service_policy_path());
    personal_vpn::broker::BrokerRuntimeRegistry registry(
        [&policy](const std::string& profile_id,
                  personal_vpn::client::ClientRuntimeControl::EventHandler event_handler)
        {
            auto config = personal_vpn::broker::load_impersonated_profile(profile_id, policy);
            return std::make_unique<personal_vpn::client::ClientRuntime>(
                std::move(config),
                std::make_unique<personal_vpn::client::WindowsNetworkBackend>(),
                std::move(event_handler));
        });

    report_status(SERVICE_RUNNING);

    std::wstring active_logon_sid;
    HANDLE server_pipe = INVALID_HANDLE_VALUE;
    DWORD server_session = no_active_session;
    while (WaitForSingleObject(stop_event, 0U) != WAIT_OBJECT_0)
    {
        const auto session_id = WTSGetActiveConsoleSessionId();
        if (session_id == no_active_session || session_id == 0U)
        {
            if (server_pipe != INVALID_HANDLE_VALUE)
            {
                close_active_pipe(server_pipe);
                server_pipe = INVALID_HANDLE_VALUE;
                server_session = no_active_session;
                active_logon_sid.clear();
                active_session_id = no_active_session;
            }
            wait_for_stop(1'000U);
            continue;
        }

        HANDLE raw_token = nullptr;
        if (!WTSQueryUserToken(session_id, &raw_token))
        {
            wait_for_stop(1'000U);
            continue;
        }
        std::unique_ptr<void, HandleCloser> token(raw_token);
        const auto logon_sid = current_logon_sid(raw_token);
        if (server_pipe == INVALID_HANDLE_VALUE || server_session != session_id ||
            active_logon_sid != logon_sid)
        {
            if (server_pipe != INVALID_HANDLE_VALUE)
            {
                close_active_pipe(server_pipe);
            }
            server_pipe = personal_vpn::broker::create_broker_pipe_server(logon_sid);
            server_session = session_id;
            active_logon_sid = logon_sid;
            active_session_id = session_id;
            {
                std::lock_guard<std::mutex> lock(pipe_mutex);
                active_pipe = server_pipe;
            }
        }

        if (!personal_vpn::broker::accept_broker_pipe_client(server_pipe))
        {
            if (WaitForSingleObject(stop_event, 0U) == WAIT_OBJECT_0)
            {
                break;
            }
            const auto current_session = WTSGetActiveConsoleSessionId();
            if (current_session != server_session)
            {
                close_active_pipe(server_pipe);
                server_pipe = INVALID_HANDLE_VALUE;
            }
            continue;
        }
        personal_vpn::broker::serve_broker_pipe_session(
            server_pipe, active_logon_sid, server_session, registry);
        if (WaitForSingleObject(stop_event, 0U) == WAIT_OBJECT_0)
        {
            break;
        }
    }
    if (server_pipe != INVALID_HANDLE_VALUE)
    {
        close_active_pipe(server_pipe);
    }
    active_session_id = no_active_session;
    registry.shutdown();
}

void WINAPI service_main(DWORD, wchar_t**)
{
    service_status_handle = RegisterServiceCtrlHandlerExW(
        service_name, service_control_handler, nullptr);
    if (service_status_handle == nullptr)
    {
        return;
    }
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event == nullptr)
    {
        report_status(SERVICE_STOPPED, GetLastError());
        return;
    }
    report_status(SERVICE_START_PENDING, NO_ERROR, 20'000U);
    try
    {
        run_broker_service();
        report_status(SERVICE_STOPPED);
    }
    catch (...)
    {
        report_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
    }
    static_cast<void>(CloseHandle(stop_event));
    stop_event = nullptr;
}

} // namespace

int wmain()
{
    SERVICE_TABLE_ENTRYW dispatch_table[]{{const_cast<LPWSTR>(service_name), service_main},
                                          {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(dispatch_table))
    {
        return static_cast<int>(GetLastError());
    }
    return 0;
}
