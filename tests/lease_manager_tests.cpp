#include "personal_vpn/lease_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace personal_vpn::core;
using namespace std::chrono_literals;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_invalid(const std::function<void()>& operation, const std::string& message)
{
    try
    {
        operation();
        check(false, message + " (no exception)");
    }
    catch (const std::invalid_argument&)
    {
    }
}

LeaseManager make_manager(const std::chrono::seconds duration = 60s)
{
    return LeaseManager(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address("10.8.0.10"),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        duration);
}

void test_ipv4_conversion()
{
    const auto address = parse_ipv4_address("192.0.2.42");
    check(address == Ipv4Address{192U, 0U, 2U, 42U}, "IPv4 parser preserves octets");
    check(format_ipv4_address(address) == "192.0.2.42", "IPv4 formatter round trip");
    expect_invalid([] { static_cast<void>(parse_ipv4_address("192.0.2")); },
                   "short IPv4 address is rejected");
    expect_invalid([] { static_cast<void>(parse_ipv4_address("192.0.2.256")); },
                   "out-of-range IPv4 component is rejected");
    expect_invalid([] { static_cast<void>(parse_ipv4_address("192.0.2.1.extra")); },
                   "long IPv4 address is rejected");
}

void test_authoritative_allocation_and_renewal()
{
    auto manager = make_manager();
    const auto now = LeaseManager::TimePoint{} + 10s;
    const auto first = manager.acquire("cert:alice", now);
    const auto second = manager.acquire("cert:bob", now);
    check(format_ipv4_address(first.address) == "10.8.0.2", "first identity gets first address");
    check(format_ipv4_address(second.address) == "10.8.0.3", "second identity gets next address");
    check(first.address != second.address, "different identities never share an active address");
    check(manager.owns("cert:alice", first.address, now), "identity owns its assigned address");
    check(!manager.owns("cert:bob", first.address, now), "identity cannot claim another address");

    const auto renewed = manager.acquire("cert:alice", now + 30s);
    check(renewed.address == first.address, "renewal preserves the identity address");
    check(renewed.expires_at == now + 90s, "renewal extends expiration");
    check(manager.active_count(now + 30s) == 2U, "renewal does not create a duplicate lease");
}

void test_release_and_expiration_reuse_addresses()
{
    auto manager = make_manager(10s);
    const auto now = LeaseManager::TimePoint{} + 100s;
    const auto alice = manager.acquire("cert:alice", now);
    check(manager.release("cert:alice"), "active lease can be released");
    check(!manager.release("cert:alice"), "released lease cannot be released twice");
    const auto bob = manager.acquire("cert:bob", now);
    check(bob.address == alice.address, "released address is reusable");

    check(manager.reap_expired(now + 10s) == 1U, "expired lease is reaped at its deadline");
    const auto carol = manager.acquire("cert:carol", now + 10s);
    check(carol.address == alice.address, "expired address is reusable");
}

void test_pool_exhaustion()
{
    LeaseManager manager(parse_ipv4_address("10.8.0.2"),
                         parse_ipv4_address("10.8.0.3"),
                         parse_ipv4_address("10.8.0.1"),
                         24U,
                         1'400U,
                         60s);
    const auto now = LeaseManager::TimePoint{};
    static_cast<void>(manager.acquire("cert:a", now));
    static_cast<void>(manager.acquire("cert:b", now));
    try
    {
        static_cast<void>(manager.acquire("cert:c", now));
        check(false, "exhausted pool rejects a new identity");
    }
    catch (const std::runtime_error&)
    {
    }
}

void test_concurrent_acquisition_is_unique()
{
    LeaseManager manager(parse_ipv4_address("10.8.0.2"),
                         parse_ipv4_address("10.8.0.33"),
                         parse_ipv4_address("10.8.0.1"),
                         24U,
                         1'400U,
                         60s);
    const auto now = LeaseManager::TimePoint{};
    std::vector<std::thread> threads;
    std::vector<Ipv4Address> addresses;
    std::mutex result_mutex;
    for (int index = 0; index < 32; ++index)
    {
        threads.emplace_back([&, index]
                             {
                                 const auto lease = manager.acquire("cert:" + std::to_string(index), now);
                                 std::lock_guard<std::mutex> lock(result_mutex);
                                 addresses.push_back(lease.address);
                             });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    std::set<Ipv4Address> unique(addresses.begin(), addresses.end());
    check(addresses.size() == 32U, "all concurrent acquisitions complete");
    check(unique.size() == 32U, "concurrent acquisitions are unique");
    check(manager.available_count(now) == 0U, "concurrent acquisitions consume the pool exactly");
}

void test_assignment_is_server_derived()
{
    auto manager = make_manager(3'600s);
    const auto lease = manager.acquire("cert:alice", LeaseManager::TimePoint{});
    const auto assignment = manager.to_assignment(lease);
    check(assignment.client_address == lease.address, "assignment uses leased client address");
    check(assignment.gateway_address == parse_ipv4_address("10.8.0.1"),
          "assignment uses configured server gateway");
    check(assignment.prefix_length == 24U && assignment.mtu == 1'400U &&
              assignment.lease_seconds == 3'600U,
          "assignment carries server policy");
}

void test_invalid_pool_policy_is_rejected()
{
    expect_invalid(
        []
        {
            LeaseManager manager(parse_ipv4_address("10.8.0.1"),
                                 parse_ipv4_address("10.8.0.10"),
                                 parse_ipv4_address("10.8.0.1"),
                                 24U,
                                 1'400U,
                                 60s);
            static_cast<void>(manager);
        },
        "pool containing gateway is rejected");
    expect_invalid(
        []
        {
            LeaseManager manager(parse_ipv4_address("10.8.1.2"),
                                 parse_ipv4_address("10.8.1.10"),
                                 parse_ipv4_address("10.8.0.1"),
                                 24U,
                                 1'400U,
                                 60s);
            static_cast<void>(manager);
        },
        "pool outside gateway subnet is rejected");
    expect_invalid(
        []
        {
            LeaseManager manager(parse_ipv4_address("10.8.0.0"),
                                 parse_ipv4_address("10.8.0.10"),
                                 parse_ipv4_address("10.8.0.1"),
                                 24U,
                                 1'400U,
                                 60s);
            static_cast<void>(manager);
        },
        "pool containing network address is rejected");
}

} // namespace

int main()
{
    test_ipv4_conversion();
    test_authoritative_allocation_and_renewal();
    test_release_and_expiration_reuse_addresses();
    test_pool_exhaustion();
    test_concurrent_acquisition_is_unique();
    test_assignment_is_server_derived();
    test_invalid_pool_policy_is_rejected();

    if (failures != 0)
    {
        std::cerr << failures << " lease-manager test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All lease-manager tests passed\n";
    return EXIT_SUCCESS;
}
