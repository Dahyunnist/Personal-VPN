#include "personal_vpn/broker_service_policy.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <optional>

namespace personal_vpn::broker
{
namespace
{

namespace pt = boost::property_tree;

void ensure_keys(const pt::ptree& tree,
                 const std::set<std::string>& allowed,
                 const char* section)
{
    std::set<std::string> seen;
    for (const auto& entry : tree)
    {
        if (entry.first.empty() || allowed.count(entry.first) == 0U ||
            !seen.insert(entry.first).second)
        {
            throw std::invalid_argument(std::string("invalid or duplicate field in ") + section);
        }
    }
}

} // namespace

client::ClientNetworkPolicy parse_broker_network_policy(const std::string_view json)
{
    if (json.empty() || json.size() > 64U * 1024U)
    {
        throw std::invalid_argument("broker policy must be non-empty and at most 64 KiB");
    }
    pt::ptree root;
    try
    {
        std::istringstream input{std::string(json)};
        pt::read_json(input, root);
    }
    catch (const pt::json_parser::json_parser_error&)
    {
        throw std::invalid_argument("broker policy is not valid JSON");
    }
    ensure_keys(root, {"schema_version", "network"}, "root");
    if (root.get_optional<std::string>("schema_version") !=
        std::optional<std::string>("1"))
    {
        throw std::invalid_argument("unsupported broker policy schema");
    }
    const auto network = root.get_child_optional("network");
    if (!network.has_value())
    {
        throw std::invalid_argument("broker policy network section is missing");
    }
    ensure_keys(*network, {"allowed_routes", "allow_default_route"}, "network");
    const auto allow_default_text = network->get_optional<std::string>("allow_default_route");
    if (!allow_default_text.has_value() ||
        (*allow_default_text != "true" && *allow_default_text != "false"))
    {
        throw std::invalid_argument("allow_default_route must be a JSON boolean");
    }

    client::ClientNetworkPolicy policy;
    policy.allow_default_route = *allow_default_text == "true";
    const auto routes = network->get_child_optional("allowed_routes");
    if (!routes.has_value() || routes->empty() || routes->size() > 64U)
    {
        throw std::invalid_argument("allowed_routes must contain 1 to 64 CIDR strings");
    }
    for (const auto& entry : *routes)
    {
        if (!entry.first.empty() || !entry.second.empty())
        {
            throw std::invalid_argument("allowed_routes must be an array of CIDR strings");
        }
        policy.allowed_routes.push_back(entry.second.get_value<std::string>());
    }
    client::validate_client_network_policy(policy);
    return policy;
}

} // namespace personal_vpn::broker
