#include "personal_vpn/windows_broker_pipe.hpp"

#ifdef _WIN32

#include <sddl.h>

#include <memory>
#include <stdexcept>
#include <string>

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

} // namespace personal_vpn::broker

#endif
