#include "windows_kill_switch.hpp"

#define WIN32_LEAN_AND_MEAN
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

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

    void install()
    {
        personal_vpn::client::WindowsVpnEndpoint ipv4{};
        ipv4.address[0U] = 127U;
        ipv4.address[3U] = 1U;
        ipv4.port = 443U;

        personal_vpn::client::WindowsVpnEndpoint ipv6{};
        ipv6.address[15U] = 1U;
        ipv6.port = 443U;
        ipv6.ipv6 = true;

        personal_vpn::client::enable_windows_kill_switch({ipv4, ipv6});
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

} // namespace

int main()
{
    const char* github_actions = std::getenv("GITHUB_ACTIONS");
    const char* enable_integration = std::getenv("PERSONAL_VPN_RUN_WFP_INTEGRATION");
    if (github_actions == nullptr || std::string(github_actions) != "true" ||
        enable_integration == nullptr || std::string(enable_integration) != "1")
    {
        std::cout << "Skipped: WFP integration is restricted to opted-in GitHub runners\n";
        return 77;
    }

    PersistentPolicy policy;
    DynamicSession session;
    try
    {
        policy.install();
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

        personal_vpn::client::close_windows_tunnel_permit(session.handle());
        if (session.handle() != nullptr)
        {
            std::cerr << "WFP dynamic session handle was not cleared\n";
            return EXIT_FAILURE;
        }
        personal_vpn::client::close_windows_tunnel_permit(session.handle());
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
