#include "personal_vpn/control_messages.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace personal_vpn::protocol;

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

void test_round_trips()
{
    const ClientHello hello{1'280U, kCapabilityIpv4};
    check(decode_client_hello(encode_client_hello(hello)) == hello, "CLIENT_HELLO round trip");

    const IpAssignment assignment{{10U, 8U, 0U, 2U},
                                  {10U, 8U, 0U, 1U},
                                  24U,
                                  1'400U,
                                  3'600U};
    check(decode_ip_assignment(encode_ip_assignment(assignment)) == assignment,
          "IP_ASSIGN round trip");

    const LivenessProbe probe{0x0102030405060708ULL};
    check(decode_liveness_probe(encode_liveness_probe(probe)) == probe,
          "liveness probe round trip");

    const ErrorMessage error{42U, "lease unavailable"};
    check(decode_error_message(encode_error_message(error)) == error, "ERROR round trip");

    const CloseMessage close{7U};
    check(decode_close_message(encode_close_message(close)) == close, "CLOSE round trip");
}

void test_invalid_lengths()
{
    expect_invalid([] { static_cast<void>(decode_client_hello({0U})); },
                   "short CLIENT_HELLO is rejected");
    expect_invalid([] { static_cast<void>(decode_ip_assignment(std::vector<std::uint8_t>(15U))); },
                   "short IP_ASSIGN is rejected");
    expect_invalid([] { static_cast<void>(decode_liveness_probe(std::vector<std::uint8_t>(9U))); },
                   "long liveness probe is rejected");
    expect_invalid([] { static_cast<void>(decode_error_message({0U})); },
                   "short ERROR is rejected");
    expect_invalid([] { static_cast<void>(decode_close_message({0U, 1U, 2U})); },
                   "long CLOSE is rejected");
}

void test_invalid_values()
{
    expect_invalid([] { static_cast<void>(encode_client_hello(ClientHello{500U, kCapabilityIpv4})); },
                   "undersized MTU is rejected");
    expect_invalid([] { static_cast<void>(encode_client_hello(ClientHello{1'400U, 0U})); },
                   "missing IPv4 capability is rejected");

    IpAssignment assignment{{10U, 8U, 0U, 2U},
                            {10U, 8U, 0U, 1U},
                            24U,
                            1'400U,
                            3'600U};
    assignment.client_address = {0U, 0U, 0U, 0U};
    expect_invalid([&] { static_cast<void>(encode_ip_assignment(assignment)); },
                   "unspecified client address is rejected");

    assignment.client_address = {10U, 8U, 0U, 2U};
    assignment.prefix_length = 33U;
    expect_invalid([&] { static_cast<void>(encode_ip_assignment(assignment)); },
                   "invalid prefix length is rejected");

    assignment.prefix_length = 24U;
    assignment.lease_seconds = 0U;
    expect_invalid([&] { static_cast<void>(encode_ip_assignment(assignment)); },
                   "zero lease duration is rejected");

    ErrorMessage long_error{1U, std::string(kMaximumErrorMessageSize + 1U, 'x')};
    expect_invalid([&] { static_cast<void>(encode_error_message(long_error)); },
                   "oversized error text is rejected");
}

void test_reserved_fields_are_rejected()
{
    auto hello = encode_client_hello(ClientHello{});
    hello[2] = 1U;
    expect_invalid([&] { static_cast<void>(decode_client_hello(hello)); },
                   "non-zero CLIENT_HELLO reserved field is rejected");

    auto assignment = encode_ip_assignment(
        IpAssignment{{10U, 8U, 0U, 2U}, {10U, 8U, 0U, 1U}, 24U, 1'400U, 3'600U});
    assignment[9] = 1U;
    expect_invalid([&] { static_cast<void>(decode_ip_assignment(assignment)); },
                   "non-zero IP_ASSIGN reserved field is rejected");
}

} // namespace

int main()
{
    test_round_trips();
    test_invalid_lengths();
    test_invalid_values();
    test_reserved_fields_are_rejected();

    if (failures != 0)
    {
        std::cerr << failures << " control-message test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All control-message tests passed\n";
    return EXIT_SUCCESS;
}
