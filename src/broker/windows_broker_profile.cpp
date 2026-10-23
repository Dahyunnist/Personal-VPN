#include "personal_vpn/windows_broker_profile.hpp"

#ifdef _WIN32

#include "personal_vpn/broker_protocol.hpp"

#include <Windows.h>
#include <userenv.h>

#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace personal_vpn::broker
{
namespace
{

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

[[noreturn]] void fail(const char* operation)
{
    throw std::system_error(static_cast<int>(GetLastError()),
                            std::system_category(),
                            operation);
}

} // namespace

std::filesystem::path resolve_impersonated_profile_path(const std::string& profile_id)
{
    if (!is_valid_profile_id(profile_id))
    {
        throw std::invalid_argument("broker profile ID is invalid");
    }
    HANDLE raw_token = nullptr;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw_token))
    {
        fail("open authenticated broker caller token");
    }
    std::unique_ptr<void, HandleCloser> token(raw_token);

    DWORD required = 0U;
    static_cast<void>(GetUserProfileDirectoryW(raw_token, nullptr, &required));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < 2U || required > 32'768U)
    {
        fail("query authenticated caller profile directory");
    }
    std::vector<wchar_t> profile_buffer(required);
    if (!GetUserProfileDirectoryW(raw_token, profile_buffer.data(), &required))
    {
        fail("read authenticated caller profile directory");
    }
    const std::filesystem::path user_profile(profile_buffer.data());
    return user_profile / L"AppData" / L"Local" / L"PersonalVPN" / L"profiles" /
           (std::filesystem::path(profile_id).wstring() + L".json");
}

} // namespace personal_vpn::broker

#endif
