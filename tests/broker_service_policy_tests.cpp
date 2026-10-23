#include "personal_vpn/broker_service_policy.hpp"

#include <cstdlib>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <Windows.h>
#include <filesystem>
#include <fstream>
#endif

namespace
{

int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Operation>
void check_rejected(Operation operation, const char* message)
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

} // namespace

int main()
{
    using personal_vpn::broker::parse_broker_network_policy;
    constexpr char valid[] =
        R"({"schema_version":1,"network":{"allowed_routes":["10.8.0.0/24","fd00:8::/64"],"allow_default_route":false}})";
    const auto policy = parse_broker_network_policy(valid);
    check(policy.allowed_routes.size() == 2U && !policy.allow_default_route,
          "loads strict versioned IPv4 and IPv6 route policy");
    check_rejected([]
                   {
                       static_cast<void>(parse_broker_network_policy(
                           R"({"schema_version":2,"network":{"allowed_routes":["10.8.0.0/24"],"allow_default_route":false}})"));
                   },
                   "rejects unsupported policy schema");
    check_rejected([]
                   {
                       static_cast<void>(parse_broker_network_policy(
                           R"({"schema_version":1,"network":{"allowed_routes":["10.8.0.1/24"],"allow_default_route":false}})"));
                   },
                   "rejects non-canonical policy routes");
    check_rejected([]
                   {
                       static_cast<void>(parse_broker_network_policy(
                           R"({"schema_version":1,"network":{"allowed_routes":["0.0.0.0/0"],"allow_default_route":false}})"));
                   },
                   "rejects default route without explicit service authorization");
    check_rejected([]
                   {
                       static_cast<void>(parse_broker_network_policy(
                           R"({"schema_version":1,"network":{"allowed_routes":["10.8.0.0/24"],"allow_default_route":false,"unexpected":true}})"));
                   },
                   "rejects unknown service policy fields");
    check_rejected([]
                   {
                       static_cast<void>(parse_broker_network_policy(
                           R"({"schema_version":1,"network":{"allowed_routes":["10.8.0.0/24","10.8.0.0/24"],"allow_default_route":false}})"));
                   },
                   "rejects duplicate policy routes");
    check_rejected([] { static_cast<void>(parse_broker_network_policy("{}")); },
                   "rejects incomplete service policy");
    check_rejected([]
                   { static_cast<void>(parse_broker_network_policy(std::string(65'537U, ' '))); },
                   "rejects oversized policy JSON");

#ifdef _WIN32
    std::array<wchar_t, MAX_PATH> temporary_directory{};
    const auto directory_size = GetTempPathW(
        static_cast<DWORD>(temporary_directory.size()), temporary_directory.data());
    std::array<wchar_t, MAX_PATH> temporary_file{};
    if (directory_size == 0U || directory_size >= temporary_directory.size() ||
        GetTempFileNameW(temporary_directory.data(), L"pvb", 0U, temporary_file.data()) == 0U)
    {
        std::cerr << "failed to create service policy ACL test file\n";
        return EXIT_FAILURE;
    }
    {
        std::ofstream output(std::filesystem::path(temporary_file.data()),
                             std::ios::binary | std::ios::trunc);
        output << valid;
    }
    bool insecure_path_rejected = false;
    try
    {
        static_cast<void>(personal_vpn::broker::load_broker_network_policy(
            std::filesystem::path(temporary_file.data())));
    }
    catch (const std::exception&)
    {
        insecure_path_rejected = true;
    }
    static_cast<void>(DeleteFileW(temporary_file.data()));
    check(insecure_path_rejected,
          "rejects service policy stored under a user-writable temporary directory");
#endif

    if (failures != 0)
    {
        return EXIT_FAILURE;
    }
    std::cout << "Broker service policy tests passed\n";
    return EXIT_SUCCESS;
}
