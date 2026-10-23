#include "windows_kill_switch.hpp"

#define WIN32_LEAN_AND_MEAN
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

class WinsockSession final
{
   public:
    WinsockSession()
    {
        const auto result = WSAStartup(MAKEWORD(2U, 2U), &data_);
        if (result != 0)
        {
            throw std::runtime_error("initialize Winsock for WFP test endpoints");
        }
    }

    ~WinsockSession() { WSACleanup(); }

    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

   private:
    WSADATA data_{};
};

bool is_public_ipv4(const std::uint8_t* const address)
{
    const auto first = address[0U];
    const auto second = address[1U];
    return first != 0U && first != 10U && first != 127U && first < 224U &&
           !(first == 100U && second >= 64U && second <= 127U) &&
           !(first == 169U && second == 254U) &&
           !(first == 172U && second >= 16U && second <= 31U) &&
           !(first == 192U && (second == 0U || second == 168U)) &&
           !(first == 198U && (second == 18U || second == 19U || second == 51U)) &&
           !(first == 203U && second == 0U);
}

bool is_public_endpoint(const personal_vpn::client::WindowsVpnEndpoint& endpoint)
{
    if (!endpoint.ipv6)
    {
        return is_public_ipv4(endpoint.address.data());
    }

    const bool ipv4_mapped =
        std::all_of(endpoint.address.begin(), endpoint.address.begin() + 10,
                    [](const std::uint8_t value) { return value == 0U; }) &&
        endpoint.address[10U] == 0xffU && endpoint.address[11U] == 0xffU;
    if (ipv4_mapped)
    {
        return is_public_ipv4(endpoint.address.data() + 12U);
    }

    return (endpoint.address[0U] & 0xe0U) == 0x20U &&
           !(endpoint.address[0U] == 0x20U && endpoint.address[1U] == 0x01U &&
             endpoint.address[2U] == 0x0dU && endpoint.address[3U] == 0xb8U);
}

personal_vpn::client::WindowsVpnEndpoint parse_endpoint(const std::string_view address_text,
                                                        const std::string_view port_text)
{
    personal_vpn::client::WindowsVpnEndpoint endpoint{};
    unsigned int port = 0U;
    const auto port_result = std::from_chars(port_text.data(),
                                             port_text.data() + port_text.size(),
                                             port);
    if (port_result.ec != std::errc{} || port_result.ptr != port_text.data() + port_text.size() ||
        port == 0U || port > 65535U)
    {
        throw std::invalid_argument("runner WFP test endpoint port is invalid");
    }
    endpoint.port = static_cast<std::uint16_t>(port);

    const std::string address(address_text);
    if (InetPtonA(AF_INET, address.c_str(), endpoint.address.data()) == 1)
    {
        endpoint.ipv6 = false;
    }
    else if (InetPtonA(AF_INET6, address.c_str(), endpoint.address.data()) == 1)
    {
        endpoint.ipv6 = true;
    }
    else
    {
        throw std::invalid_argument("runner WFP test endpoint address is invalid");
    }
    if (!is_public_endpoint(endpoint))
    {
        throw std::invalid_argument("runner WFP test endpoint must use a public address");
    }
    return endpoint;
}

std::vector<personal_vpn::client::WindowsVpnEndpoint> test_endpoints()
{
    std::vector<personal_vpn::client::WindowsVpnEndpoint> endpoints;
    personal_vpn::client::WindowsVpnEndpoint ipv4{};
    ipv4.address[0U] = 127U;
    ipv4.address[3U] = 1U;
    ipv4.port = 443U;
    endpoints.push_back(ipv4);

    personal_vpn::client::WindowsVpnEndpoint ipv6{};
    ipv6.address[15U] = 1U;
    ipv6.port = 443U;
    ipv6.ipv6 = true;
    endpoints.push_back(ipv6);

    char encoded_endpoints[8192U]{};
    const auto encoded_length = GetEnvironmentVariableA(
        "PERSONAL_VPN_TEST_PERMIT_ENDPOINTS",
        encoded_endpoints,
        static_cast<DWORD>(sizeof(encoded_endpoints)));
    if (encoded_length == 0U || encoded_length >= sizeof(encoded_endpoints))
    {
        throw std::runtime_error("runner control-plane endpoints are missing or too long");
    }

    std::string_view remaining(encoded_endpoints, encoded_length);
    while (!remaining.empty())
    {
        const auto record_end = remaining.find(';');
        const auto record = remaining.substr(0U, record_end);
        const auto separator = record.find('|');
        if (separator == std::string_view::npos)
        {
            throw std::invalid_argument("runner WFP test endpoint record is malformed");
        }
        const auto endpoint = parse_endpoint(record.substr(0U, separator),
                                             record.substr(separator + 1U));
        const auto duplicate = std::find_if(endpoints.begin(), endpoints.end(),
                                            [&endpoint](const auto& candidate) {
                                                return candidate.address == endpoint.address &&
                                                       candidate.port == endpoint.port &&
                                                       candidate.ipv6 == endpoint.ipv6;
                                            });
        if (duplicate == endpoints.end())
        {
            if (endpoints.size() == 32U)
            {
                throw std::invalid_argument("too many runner WFP test endpoints");
            }
            endpoints.push_back(endpoint);
        }
        if (record_end == std::string_view::npos)
        {
            break;
        }
        remaining.remove_prefix(record_end + 1U);
    }
    return endpoints;
}

class DynamicSession final
{
   public:
    ~DynamicSession() { personal_vpn::client::close_windows_tunnel_permit(handle_); }

    DynamicSession() = default;
    DynamicSession(const DynamicSession&) = delete;
    DynamicSession& operator=(const DynamicSession&) = delete;

    void*& handle() noexcept { return handle_; }

   private:
    void* handle_{nullptr};
};

class PersistentPolicy final
{
   public:
    ~PersistentPolicy()
    {
        if (installed_)
        {
            try
            {
                personal_vpn::client::disable_windows_kill_switch();
            }
            catch (...)
            {
            }
        }
    }

    void install(const std::vector<personal_vpn::client::WindowsVpnEndpoint>& endpoints)
    {
        personal_vpn::client::enable_windows_kill_switch(endpoints);
        installed_ = true;
    }

    void remove()
    {
        personal_vpn::client::disable_windows_kill_switch();
        installed_ = false;
    }

   private:
    bool installed_{false};
};

bool environment_variable_equals(const char* name, const char* expected)
{
    char value[16U]{};
    const auto length = GetEnvironmentVariableA(
        name, value, static_cast<DWORD>(sizeof(value)));
    return length > 0U && length < sizeof(value) && std::strcmp(value, expected) == 0;
}

} // namespace

int main(const int argc, char** argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--cleanup") == 0)
    {
        try
        {
            personal_vpn::client::disable_windows_kill_switch();
            std::cout << "Windows WFP test policy cleanup passed\n";
            return EXIT_SUCCESS;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Windows WFP test policy cleanup failed: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    if (argc == 2 && std::strcmp(argv[1], "--validate-endpoints") == 0)
    {
        try
        {
            const WinsockSession winsock;
            const auto endpoints = test_endpoints();
            std::cout << "Parsed " << endpoints.size()
                      << " dual-stack WFP test endpoints\n";
            return EXIT_SUCCESS;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Invalid runner WFP test endpoints: " << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }
    if (argc != 1)
    {
        std::cerr << "unknown argument\n";
        return EXIT_FAILURE;
    }

    if (!environment_variable_equals("GITHUB_ACTIONS", "true") ||
        !environment_variable_equals("PERSONAL_VPN_RUN_WFP_INTEGRATION", "1"))
    {
        std::cout << "Skipped: WFP integration is restricted to opted-in GitHub runners\n";
        return 77;
    }

    PersistentPolicy policy;
    DynamicSession session;
    try
    {
        const WinsockSession winsock;
        const auto endpoints = test_endpoints();
        std::cout << "Allowing " << endpoints.size() - 2U
                  << " active runner control-plane endpoints during test\n" << std::flush;
        std::cout << "Installing persistent dual-stack WFP test policy\n" << std::flush;
        policy.install(endpoints);
        std::cout << "Installing dynamic dual-stack tunnel permits\n" << std::flush;
        personal_vpn::client::replace_windows_tunnel_permit(session.handle(), 1U);
        if (session.handle() == nullptr)
        {
            std::cerr << "WFP dynamic session was not retained\n";
            return EXIT_FAILURE;
        }

        void* const prior_session = session.handle();
        try
        {
            personal_vpn::client::replace_windows_tunnel_permit(session.handle(), 0U);
            std::cerr << "zero interface index was accepted\n";
            return EXIT_FAILURE;
        }
        catch (const std::invalid_argument&)
        {
        }
        if (session.handle() != prior_session)
        {
            std::cerr << "invalid replacement discarded active WFP session\n";
            return EXIT_FAILURE;
        }

        std::cout << "Closing dynamic WFP session\n" << std::flush;
        personal_vpn::client::close_windows_tunnel_permit(session.handle());
        if (session.handle() != nullptr)
        {
            std::cerr << "WFP dynamic session handle was not cleared\n";
            return EXIT_FAILURE;
        }
        personal_vpn::client::close_windows_tunnel_permit(session.handle());
        std::cout << "Removing persistent WFP test policy\n" << std::flush;
        policy.remove();
        std::cout << "Windows WFP dynamic session lifecycle passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Windows WFP dynamic session lifecycle failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
