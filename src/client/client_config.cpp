#include "personal_vpn/client_config.hpp"

#include "personal_vpn/credential_security.hpp"
#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/protocol.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace personal_vpn::client
{
namespace
{
namespace pt = boost::property_tree;

constexpr std::uintmax_t kMaximumConfigBytes = 64U * 1024U;

void ensure_keys(const pt::ptree& tree,
                 const std::set<std::string>& allowed,
                 const std::string& section)
{
    std::set<std::string> seen;
    for (const auto& entry : tree)
    {
        if (entry.first.empty() || allowed.count(entry.first) == 0U)
        {
            throw std::invalid_argument("unknown field in " + section + ": " + entry.first);
        }
        if (!seen.insert(entry.first).second)
        {
            throw std::invalid_argument("duplicate field in " + section + ": " + entry.first);
        }
    }
}

const pt::ptree* require_section(const pt::ptree& root, const std::string& name)
{
    const auto section = root.get_child_optional(name);
    if (!section.has_value() || section->empty())
    {
        throw std::invalid_argument("missing or empty configuration section: " + name);
    }
    return &section.get();
}

std::string require_text(const pt::ptree& tree,
                         const std::string& field,
                         const std::size_t maximum_length)
{
    const auto value = tree.get_optional<std::string>(field);
    if (!value.has_value() || value->empty() || value->size() > maximum_length)
    {
        throw std::invalid_argument("missing or invalid text field: " + field);
    }
    for (const auto character : *value)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (std::iscntrl(byte) != 0 || std::isspace(byte) != 0)
        {
            throw std::invalid_argument("whitespace and control bytes are forbidden in: " + field);
        }
    }
    return *value;
}

std::uint64_t require_unsigned(const pt::ptree& tree,
                               const std::string& field,
                               const std::uint64_t minimum,
                               const std::uint64_t maximum)
{
    const auto text = tree.get_optional<std::string>(field);
    std::uint64_t value = 0U;
    if (!text.has_value())
    {
        throw std::invalid_argument("missing numeric field: " + field);
    }
    const auto result = std::from_chars(text->data(), text->data() + text->size(), value, 10);
    if (text->empty() || result.ec != std::errc{} ||
        result.ptr != text->data() + text->size() || value < minimum || value > maximum)
    {
        throw std::invalid_argument("numeric field is out of range: " + field);
    }
    return value;
}

bool optional_boolean(const pt::ptree& tree, const std::string& field, const bool fallback)
{
    const auto text = tree.get_optional<std::string>(field);
    if (!text.has_value())
    {
        return fallback;
    }
    if (*text == "true")
    {
        return true;
    }
    if (*text == "false")
    {
        return false;
    }
    throw std::invalid_argument("boolean field must be true or false: " + field);
}

std::filesystem::path credential_path(const std::filesystem::path& config_directory,
                                      const pt::ptree& tls,
                                      const std::string& field)
{
    auto path = std::filesystem::path(require_text(tls, field, 4'096U));
    if (path.is_relative())
    {
        path = config_directory / path;
    }
    path = std::filesystem::absolute(path).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        throw std::invalid_argument("credential is not a readable regular file: " + field);
    }
    return path;
}

std::string validate_route(const std::string& route, const bool allow_default_route)
{
    if (route.find(':') != std::string::npos)
    {
        const auto slash = route.find('/');
        if (slash == std::string::npos || slash == 0U || slash + 1U >= route.size())
        {
            throw std::invalid_argument("route must use IPv6 CIDR notation: " + route);
        }
        const auto prefix_text = route.substr(slash + 1U);
        std::uint16_t prefix = 0U;
        const auto prefix_result = std::from_chars(prefix_text.data(),
                                                   prefix_text.data() + prefix_text.size(),
                                                   prefix,
                                                   10);
        if (prefix_result.ec != std::errc{} ||
            prefix_result.ptr != prefix_text.data() + prefix_text.size() || prefix > 128U)
        {
            throw std::invalid_argument("IPv6 route prefix is out of range: " + route);
        }
        if (prefix == 0U && !allow_default_route)
        {
            throw std::invalid_argument("a default route requires tunnel.allow_default_route=true");
        }
        boost::system::error_code address_error;
        const auto address = boost::asio::ip::make_address_v6(route.substr(0U, slash), address_error);
        if (address_error || address.scope_id() != 0U)
        {
            throw std::invalid_argument("IPv6 route address is invalid: " + route);
        }
        auto network = address.to_bytes();
        auto canonical = network;
        for (std::size_t bit = prefix; bit < 128U; ++bit)
        {
            canonical[bit / 8U] = static_cast<std::uint8_t>(
                canonical[bit / 8U] & static_cast<std::uint8_t>(~(1U << (7U - bit % 8U))));
        }
        if (network != canonical)
        {
            throw std::invalid_argument("route address must be the canonical network address: " + route);
        }
        return route;
    }
    const auto slash = route.find('/');
    if (slash == std::string::npos || slash == 0U || slash + 1U >= route.size() ||
        route.find('/', slash + 1U) != std::string::npos)
    {
        throw std::invalid_argument("route must use IPv4 or IPv6 CIDR notation: " + route);
    }
    const auto address_text = route.substr(0U, slash);
    const auto prefix_text = route.substr(slash + 1U);
    std::uint16_t prefix = 0U;
    const auto prefix_result = std::from_chars(prefix_text.data(),
                                               prefix_text.data() + prefix_text.size(),
                                               prefix,
                                               10);
    if (prefix_result.ec != std::errc{} ||
        prefix_result.ptr != prefix_text.data() + prefix_text.size() || prefix > 32U)
    {
        throw std::invalid_argument("route prefix is out of range: " + route);
    }
    if (prefix == 0U && !allow_default_route)
    {
        throw std::invalid_argument("a default route requires tunnel.allow_default_route=true");
    }
    const auto address = core::parse_ipv4_address(address_text);
    std::uint32_t value = 0U;
    for (const auto octet : address)
    {
        value = (value << 8U) | octet;
    }
    const auto mask = prefix == 0U ? 0U : std::numeric_limits<std::uint32_t>::max()
                                                << static_cast<unsigned int>(32U - prefix);
    if ((value & mask) != value)
    {
        throw std::invalid_argument("route address must be the canonical network address: " +
                                    route);
    }
    return route;
}

} // namespace

static ClientConfig load_client_config_impl(const std::filesystem::path& config_file,
                                            const ClientNetworkPolicy* policy)
{
    std::set<std::string> allowed_routes;
    if (policy != nullptr)
    {
        if (policy->allowed_routes.empty() || policy->allowed_routes.size() > 64U)
        {
            throw std::invalid_argument("trusted network policy must contain 1 to 64 routes");
        }
        for (const auto& route : policy->allowed_routes)
        {
            if (!allowed_routes.insert(validate_route(route, policy->allow_default_route)).second)
            {
                throw std::invalid_argument("trusted network policy contains duplicate routes");
            }
        }
    }
    const auto absolute_config = std::filesystem::absolute(config_file).lexically_normal();
    std::error_code file_error;
    const auto size = std::filesystem::file_size(absolute_config, file_error);
    if (file_error || size == 0U || size > kMaximumConfigBytes)
    {
        throw std::invalid_argument("configuration must be a non-empty regular file up to 64 KiB");
    }

    pt::ptree root;
    try
    {
        pt::read_json(absolute_config.string(), root);
    }
    catch (const pt::json_parser::json_parser_error&)
    {
        throw std::invalid_argument("configuration is not valid JSON");
    }
    ensure_keys(root, {"schema_version", "server", "tls", "tunnel"}, "root");
    if (require_unsigned(root, "schema_version", 1U, 1U) != 1U)
    {
        throw std::invalid_argument("unsupported client configuration schema");
    }

    const auto& server = *require_section(root, "server");
    ensure_keys(server, {"host", "port", "expected_name"}, "server");
    const auto& tls = *require_section(root, "tls");
    ensure_keys(tls,
                {"ca_file", "certificate_chain_file", "private_key_file"},
                "tls");
    const auto& tunnel = *require_section(root, "tunnel");
    ensure_keys(tunnel, {"requested_mtu", "routes", "allow_default_route"}, "tunnel");

    ClientConfig config;
    config.server_host = require_text(server, "host", 253U);
    config.server_port = static_cast<std::uint16_t>(require_unsigned(
        server, "port", 1U, std::numeric_limits<std::uint16_t>::max()));
    config.expected_server_name = require_text(server, "expected_name", 253U);
    const auto directory = absolute_config.parent_path();
    config.ca_file = credential_path(directory, tls, "ca_file");
    config.certificate_chain_file = credential_path(directory, tls, "certificate_chain_file");
    config.private_key_file = credential_path(directory, tls, "private_key_file");
    validate_private_key_permissions(config.private_key_file);
    config.requested_mtu = static_cast<std::uint16_t>(require_unsigned(
        tunnel,
        "requested_mtu",
        protocol::kMinimumIpv6Mtu,
        protocol::kMaximumTunnelMtu));
    config.allow_default_route = optional_boolean(tunnel, "allow_default_route", false);
    if (policy != nullptr && config.allow_default_route && !policy->allow_default_route)
    {
        throw std::invalid_argument("profile requests a default route not authorized by policy");
    }

    const auto routes = tunnel.get_child_optional("routes");
    if (!routes.has_value() || routes->empty() || routes->size() > 64U)
    {
        throw std::invalid_argument("tunnel.routes must contain between 1 and 64 routes");
    }
    std::set<std::string> unique_routes;
    for (const auto& entry : *routes)
    {
        if (!entry.first.empty() || !entry.second.empty())
        {
            throw std::invalid_argument("tunnel.routes must be an array of CIDR strings");
        }
        const auto route = validate_route(entry.second.get_value<std::string>(),
                                          config.allow_default_route);
        if (policy != nullptr && allowed_routes.count(route) == 0U)
        {
            throw std::invalid_argument("profile route is not authorized by network policy");
        }
        if (!unique_routes.insert(route).second)
        {
            throw std::invalid_argument("duplicate route: " + route);
        }
        config.routes.push_back(route);
    }
    if (config.allow_default_route &&
        (std::find(config.routes.begin(), config.routes.end(), "0.0.0.0/0") !=
             config.routes.end() ||
         std::find(config.routes.begin(), config.routes.end(), "::/0") != config.routes.end()))
    {
        boost::system::error_code address_error;
        const auto address = boost::asio::ip::make_address(config.server_host, address_error);
        if (address_error || (address.is_v6() && address.to_v6().scope_id() != 0U))
        {
            throw std::invalid_argument(
                "full-tunnel mode requires a numeric server IP to prevent DNS leakage");
        }
    }
    return config;
}

ClientConfig load_client_config(const std::filesystem::path& config_file)
{
    return load_client_config_impl(config_file, nullptr);
}

ClientConfig load_client_config(const std::filesystem::path& config_file,
                                const ClientNetworkPolicy& policy)
{
    return load_client_config_impl(config_file, &policy);
}

} // namespace personal_vpn::client
