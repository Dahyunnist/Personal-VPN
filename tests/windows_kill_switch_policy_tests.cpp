#include "windows_kill_switch_policy.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace personal_vpn::client;
using namespace personal_vpn::client::detail;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Operation>
void expect_invalid(Operation&& operation, const std::string& message)
{
    try
    {
        operation();
        check(false, message);
    }
    catch (const std::invalid_argument&)
    {
    }
}

WindowsVpnEndpoint endpoint(const bool ipv6, const std::uint8_t last_octet,
                            const std::uint16_t port)
{
    WindowsVpnEndpoint value{};
    value.ipv6 = ipv6;
    value.address[ipv6 ? 15U : 3U] = last_octet;
    value.port = port;
    return value;
}

void test_dual_stack_policy()
{
    const std::vector<WindowsVpnEndpoint> endpoints{
        endpoint(false, 10U, 443U),
        endpoint(true, 11U, 8443U),
        endpoint(false, 12U, 443U),
    };
    const auto policies = build_kill_switch_policy(endpoints);

    check(policies.size() == 2U, "kill switch creates policy for both IP families");
    check(policies[0].family == IpFamily::IPv4, "IPv4 policy is first");
    check(policies[1].family == IpFamily::IPv6, "IPv6 policy is second");
    check(policies[0].endpoints.size() == 2U, "IPv4 endpoints stay in IPv4 policy");
    check(policies[1].endpoints.size() == 1U, "IPv6 endpoint stays in IPv6 policy");
    for (const auto& policy : policies)
    {
        check(policy.boot_time_ip_block, "boot-time block covers each family");
        check(policy.persistent_transport_block,
              "persistent transport block covers each family");
        check(policy.persistent_ip_block, "persistent packet block covers each family");
        for (const auto& vpn_endpoint : policy.endpoints)
        {
            check(vpn_endpoint.ipv6 == (policy.family == IpFamily::IPv6),
                  "endpoint permit cannot cross IP-family policy");
        }
    }
}

void test_fail_closed_validation()
{
    expect_invalid([] { static_cast<void>(build_kill_switch_policy({})); },
                   "empty endpoint set rejected before WFP changes");

    auto zero_port = endpoint(false, 1U, 0U);
    expect_invalid([&] { static_cast<void>(build_kill_switch_policy({zero_port})); },
                   "zero endpoint port rejected");

    auto zero_ipv4 = WindowsVpnEndpoint{};
    zero_ipv4.port = 443U;
    expect_invalid([&] { static_cast<void>(build_kill_switch_policy({zero_ipv4})); },
                   "zero IPv4 endpoint rejected");

    auto zero_ipv6 = WindowsVpnEndpoint{};
    zero_ipv6.ipv6 = true;
    zero_ipv6.port = 443U;
    expect_invalid([&] { static_cast<void>(build_kill_switch_policy({zero_ipv6})); },
                   "zero IPv6 endpoint rejected");

    std::vector<WindowsVpnEndpoint> too_many_endpoints(33U, endpoint(false, 1U, 443U));
    expect_invalid(
        [&] { static_cast<void>(build_kill_switch_policy(too_many_endpoints)); },
        "endpoint count above policy limit rejected");

    expect_invalid([] { static_cast<void>(build_tunnel_permit_policy(0U)); },
                   "zero tunnel interface index rejected");
}

void test_tunnel_permit_covers_both_families()
{
    const auto policy = build_tunnel_permit_policy(42U);
    check(policy.interface_index == 42U, "tunnel permit uses requested interface");
    check(policy.families == std::array<IpFamily, 2U>{IpFamily::IPv4, IpFamily::IPv6},
          "tunnel permit covers IPv4 and IPv6");
}

} // namespace

int main()
{
    test_dual_stack_policy();
    test_fail_closed_validation();
    test_tunnel_permit_covers_both_families();
    if (failures != 0)
    {
        std::cerr << failures << " Windows kill-switch policy test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Windows kill-switch policy tests passed\n";
    return EXIT_SUCCESS;
}
