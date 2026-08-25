#include "personal_vpn/client_network_transaction.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace personal_vpn::client;
using personal_vpn::protocol::IpAssignment;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class FakeBackend final : public ClientNetworkBackend
{
   public:
    void open_adapter() override
    {
        events.push_back("open");
        fail_if(1);
    }

    void start_packet_session() override
    {
        events.push_back("start-session");
        fail_if(2);
    }

    bool add_interface_address(const InterfaceAddress&) override
    {
        events.push_back("add-address");
        fail_if(3);
        return create_address;
    }

    std::optional<std::uint32_t> set_interface_mtu(const std::uint32_t)
        override
    {
        events.push_back("set-mtu");
        fail_if(4);
        return prior_mtu;
    }

    bool add_route(const Ipv4Route& route) override
    {
        events.push_back("add-route-" + std::to_string(route.prefix_length));
        ++route_calls;
        fail_if(4 + route_calls);
        return route_calls != preexisting_route_call;
    }

    void remove_route(const Ipv4Route& route) noexcept override
    {
        events.push_back("remove-route-" + std::to_string(route.prefix_length));
    }

    void restore_interface_mtu(const std::uint32_t) noexcept override
    {
        events.push_back("restore-mtu");
    }

    void remove_interface_address(const InterfaceAddress&) noexcept override
    {
        events.push_back("remove-address");
    }

    void stop_packet_session() noexcept override { events.push_back("stop-session"); }
    void close_adapter() noexcept override { events.push_back("close"); }

    void fail_if(const int step) const
    {
        if (failure_step == step)
        {
            throw std::runtime_error("injected backend failure");
        }
    }

    std::vector<std::string> events;
    int failure_step{0};
    int route_calls{0};
    int preexisting_route_call{0};
    bool create_address{true};
    std::optional<std::uint32_t> prior_mtu{1'500U};
};

IpAssignment assignment()
{
    IpAssignment value;
    value.client_address = {10U, 8U, 0U, 2U};
    value.gateway_address = {10U, 8U, 0U, 1U};
    value.prefix_length = 24U;
    value.mtu = 1'400U;
    return value;
}

void successful_transaction_rolls_back_exactly()
{
    FakeBackend backend;
    backend.preexisting_route_call = 1;
    backend.create_address = false;
    ClientNetworkTransaction transaction(backend);
    transaction.activate(assignment(), {"10.20.0.0/16", "192.0.2.0/24"});
    check(transaction.active(), "complete network setup becomes active");
    transaction.rollback();
    check(!transaction.active(), "explicit rollback clears active state");
    const std::vector<std::string> expected{"open",
                                            "start-session",
                                            "add-address",
                                            "set-mtu",
                                            "add-route-16",
                                            "add-route-24",
                                            "remove-route-24",
                                            "restore-mtu",
                                            "stop-session",
                                            "close"};
    check(backend.events == expected,
          "rollback removes only resources created by this transaction in reverse order");
    transaction.rollback();
    check(backend.events == expected, "rollback is idempotent");
}

void each_failure_rolls_back_completed_steps()
{
    for (int failure_step = 1; failure_step <= 6; ++failure_step)
    {
        FakeBackend backend;
        backend.failure_step = failure_step;
        ClientNetworkTransaction transaction(backend);
        bool threw = false;
        try
        {
            transaction.activate(assignment(), {"10.20.0.0/16", "192.0.2.0/24"});
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        check(threw && !transaction.active(),
              "injected setup failure fails closed at step " + std::to_string(failure_step));
        const auto released = failure_step == 1 ? backend.events.back() == "open"
                                                : backend.events.back() == "close";
        check(released,
              "failure releases every completed setup step at step " +
                  std::to_string(failure_step));
    }
}

} // namespace

int main()
{
    successful_transaction_rolls_back_exactly();
    each_failure_rolls_back_completed_steps();
    if (failures != 0)
    {
        std::cerr << failures << " client network transaction test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All client network transaction tests passed\n";
    return EXIT_SUCCESS;
}
