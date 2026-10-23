#include "personal_vpn/client_config.hpp"
#include "personal_vpn/client_tls_security.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using personal_vpn::client::load_client_config;
using personal_vpn::client::make_client_tls_context;
using personal_vpn::client::ClientNetworkPolicy;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write_config(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("failed to create test client configuration");
    }
    output << content;
}

bool rejects(const std::filesystem::path& path, const std::string& content)
{
    write_config(path, content);
    try
    {
        static_cast<void>(load_client_config(path));
        return false;
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
}

std::string valid_config(const std::string& extra_root = "",
                         const std::string& routes = "[\"10.20.0.0/16\"]",
                         const std::string& allow_default = "false",
                         const std::string& tls_extension = "",
                         const unsigned int schema_version = 1U)
{
    return "{\n"
           "  \"schema_version\": " + std::to_string(schema_version) + ",\n"
           "  \"server\": {\"host\": \"127.0.0.1\", \"port\": 8443, "
           "\"expected_name\": \"vpn.example.test\"},\n"
           "  \"tls\": {\"ca_file\": \"ca.crt\", "
           "\"certificate_chain_file\": \"client.crt\", "
           "\"private_key_file\": \"client.key\"" + tls_extension + "},\n"
           "  \"tunnel\": {\"requested_mtu\": 1400, \"routes\": " + routes +
           ", \"allow_default_route\": " + allow_default + "}" + extra_root + "\n}\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "expected test PKI directory\n";
        return EXIT_FAILURE;
    }
    const auto directory = std::filesystem::absolute(argv[1]);
    const auto path = directory / "client-config-test.json";

    write_config(path, valid_config());
    const auto config = load_client_config(path);
    check(config.server_host == "127.0.0.1" && config.server_port == 8'443U &&
              config.expected_server_name == "vpn.example.test",
          "strict config loads connection and server identity fields");
    check(config.ca_file == (directory / "ca.crt").lexically_normal() &&
              config.certificate_chain_file == (directory / "client.crt").lexically_normal() &&
              config.private_key_file == (directory / "client.key").lexically_normal(),
          "relative credential paths resolve against the configuration directory");
    check(config.routes.size() == 1U && config.routes.front() == "10.20.0.0/16" &&
              !config.allow_default_route,
          "split-tunnel route policy is retained without client-selected tunnel addresses");
    auto tls_context = make_client_tls_context(config);
    check(tls_context.native_handle() != nullptr,
          "validated path configuration creates the mandatory mutual-TLS context");

    write_config(path,
                 valid_config("",
                              "[\"10.20.0.0/16\"]",
                              "false",
                              ", \"server_crl_file\": \"client.crl\"",
                              2U));
    const auto config_with_crl = load_client_config(path);
    check(config_with_crl.server_crl_file == (directory / "client.crl").lexically_normal(),
          "schema version 2 resolves its optional server CRL path relative to the profile");
    check(make_client_tls_context(config_with_crl).native_handle() != nullptr,
          "schema version 2 loads a validated server CRL into TLS");
    check(rejects(path,
                  valid_config("",
                               "[\"10.20.0.0/16\"]",
                               "false",
                               ", \"server_crl_file\": \"\"",
                               2U)),
          "schema version 2 rejects an empty server CRL path");
    check(rejects(path, valid_config("", "[\"10.20.0.0/16\"]", "false", "", 2U)),
          "schema version 2 requires its explicit server CRL field");
    check(rejects(path,
                  valid_config("",
                               "[\"10.20.0.0/16\"]",
                               "false",
                               ", \"server_crl_file\": \"missing.crl\"",
                               2U)),
          "schema version 2 rejects a missing server CRL file");
    const auto invalid_crl = directory / "invalid-server.crl";
    write_config(invalid_crl, "not a PEM CRL\n");
    write_config(path,
                 valid_config("",
                              "[\"10.20.0.0/16\"]",
                              "false",
                              ", \"server_crl_file\": \"invalid-server.crl\"",
                              2U));
    bool invalid_crl_rejected = false;
    try
    {
        static_cast<void>(make_client_tls_context(load_client_config(path)));
    }
    catch (const std::runtime_error&)
    {
        invalid_crl_rejected = true;
    }
    check(invalid_crl_rejected,
          "schema version 2 fails closed when OpenSSL cannot parse its server CRL");
    std::filesystem::remove(invalid_crl);
    const auto oversized_crl = directory / "oversized-server.crl";
    std::ofstream(oversized_crl, std::ios::binary).close();
    std::filesystem::resize_file(oversized_crl, 8U * 1024U * 1024U + 1U);
    check(rejects(path,
                  valid_config("",
                               "[\"10.20.0.0/16\"]",
                               "false",
                               ", \"server_crl_file\": \"oversized-server.crl\"",
                               2U)),
          "schema version 2 rejects server CRL files larger than 8 MiB");
    std::filesystem::remove(oversized_crl);

    check(rejects(path, valid_config(",\n  \"certs\": {\"client_key\": \"secret\"}")),
          "legacy embedded credential fields are rejected");
    check(rejects(path, valid_config(",\n  \"unexpected\": true")),
          "unknown root fields are rejected instead of silently ignored");
    check(rejects(path, valid_config("", "[\"10.20.1.2/16\"]")),
          "non-canonical route networks are rejected");
    check(rejects(path, valid_config("", "[\"0.0.0.0/0\"]")),
          "default route requires explicit opt-in");

    write_config(path, valid_config("", "[\"0.0.0.0/0\"]", "true"));
    check(load_client_config(path).allow_default_route,
          "numeric server IP permits explicitly authorized full-tunnel configuration");
    const ClientNetworkPolicy split_tunnel_policy{{"10.20.0.0/16"}, false};
    write_config(path, valid_config());
    check(load_client_config(path, split_tunnel_policy).routes ==
              std::vector<std::string>{"10.20.0.0/16"},
          "trusted route policy accepts an authorized profile route");
    write_config(path, valid_config("", "[\"10.30.0.0/16\"]"));
    bool unauthorized_route_rejected = false;
    try
    {
        static_cast<void>(load_client_config(path, split_tunnel_policy));
    }
    catch (const std::invalid_argument&)
    {
        unauthorized_route_rejected = true;
    }
    check(unauthorized_route_rejected,
          "trusted route policy rejects a user-selected unauthorized prefix");
    write_config(path, valid_config("", "[\"0.0.0.0/0\"]", "true"));
    bool unauthorized_default_rejected = false;
    try
    {
        static_cast<void>(load_client_config(path, split_tunnel_policy));
    }
    catch (const std::invalid_argument&)
    {
        unauthorized_default_rejected = true;
    }
    check(unauthorized_default_rejected,
          "trusted route policy rejects a user-selected default route");
    auto hostname_full_tunnel = valid_config("", "[\"0.0.0.0/0\"]", "true");
    const auto address_position = hostname_full_tunnel.find("127.0.0.1");
    hostname_full_tunnel.replace(address_position, std::string("127.0.0.1").size(),
                                 "vpn.example.test");
    check(rejects(path, hostname_full_tunnel),
          "full-tunnel mode rejects DNS hostnames to prevent resolver leakage");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    if (failures != 0)
    {
        std::cerr << failures << " client configuration test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All client configuration tests passed\n";
    return EXIT_SUCCESS;
}
