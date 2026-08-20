#include "personal_vpn/server_config.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using personal_vpn::server::parse_server_arguments;
using personal_vpn::server::server_usage;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_invalid(const std::vector<std::string>& arguments, const std::string& message)
{
    try
    {
        static_cast<void>(parse_server_arguments(arguments));
        check(false, message + " (no error)");
    }
    catch (const std::invalid_argument&)
    {
    }
}

std::vector<std::string> credentials()
{
    return {"--server-cert", "server.crt", "--server-key", "server.key", "--client-ca", "ca.crt"};
}

void test_defaults_and_overrides()
{
    const auto defaults = parse_server_arguments(credentials());
    check(defaults.listen_address == "0.0.0.0" && defaults.listen_port == 8'443U &&
              defaults.tun_name == "pvpn0" && defaults.maximum_sessions == 1'024U,
          "safe runtime defaults are applied");

    auto arguments = credentials();
    const std::vector<std::string> overrides{
        "--listen-address", "127.0.0.1", "--port", "9443", "--tun-name", "pvpn-test",
        "--lease-start", "10.9.0.10", "--lease-end", "10.9.0.20", "--gateway",
        "10.9.0.1", "--prefix-length", "24", "--mtu", "1280", "--lease-seconds",
        "120", "--threads", "4", "--max-sessions", "64"};
    arguments.insert(arguments.end(), overrides.begin(), overrides.end());
    const auto config = parse_server_arguments(arguments);
    check(config.listen_address == "127.0.0.1" && config.listen_port == 9'443U &&
              config.first_lease_address == "10.9.0.10" && config.worker_threads == 4U &&
              config.maximum_sessions == 64U,
          "explicit runtime options override defaults");
}

void test_invalid_input_fails_before_privileged_io()
{
    expect_invalid({}, "credential paths are mandatory");
    expect_invalid({"--server-cert"}, "missing option value is rejected");
    expect_invalid({"--unknown", "value"}, "unknown option is rejected");

    auto invalid_port = credentials();
    invalid_port.insert(invalid_port.end(), {"--port", "0"});
    expect_invalid(invalid_port, "zero port is rejected");

    auto invalid_address = credentials();
    invalid_address.insert(invalid_address.end(), {"--listen-address", "not-an-ip"});
    expect_invalid(invalid_address, "invalid listen address is rejected");

    auto invalid_pool = credentials();
    invalid_pool.insert(invalid_pool.end(), {"--lease-start", "10.8.1.2"});
    expect_invalid(invalid_pool, "lease pool outside gateway subnet is rejected");

    auto invalid_threads = credentials();
    invalid_threads.insert(invalid_threads.end(), {"--threads", "257"});
    expect_invalid(invalid_threads, "excessive worker count is rejected");
}

void test_help_needs_no_credentials()
{
    const auto config = parse_server_arguments({"--help"});
    check(config.show_help, "help can be requested without privileged configuration");
    const auto usage = server_usage("personal-vpn-server");
    check(usage.find("--client-ca") != std::string::npos &&
              usage.find("--max-sessions") != std::string::npos,
          "usage documents security and admission options");
}

} // namespace

int main()
{
    test_defaults_and_overrides();
    test_invalid_input_fails_before_privileged_io();
    test_help_needs_no_credentials();
    if (failures != 0)
    {
        std::cerr << failures << " server-config test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All server-config tests passed\n";
    return EXIT_SUCCESS;
}
