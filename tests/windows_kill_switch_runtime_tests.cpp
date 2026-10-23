#include "windows_kill_switch.hpp"

#define WIN32_LEAN_AND_MEAN
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <system_error>

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

} // namespace

int main()
{
    try
    {
        DynamicSession session;
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
        std::cout << "Windows WFP dynamic session lifecycle passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::system_error& error)
    {
        if (error.code().value() == static_cast<int>(ERROR_ACCESS_DENIED))
        {
            std::cout << "Skipped: WFP dynamic-session test requires elevation\n";
            return 77;
        }
        std::cerr << "Windows WFP dynamic session lifecycle failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Windows WFP dynamic session lifecycle failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
