#include "personal_vpn/client_network_transaction.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace personal_vpn::client
{
namespace
{

Ipv4Route parse_route(const std::string& cidr, const core::Ipv4Address& gateway)
{
    const auto slash = cidr.find('/');
    if (slash == std::string::npos || slash == 0U || slash + 1U >= cidr.size())
    {
        throw std::invalid_argument("route must use IPv4 CIDR notation");
    }
    std::uint16_t prefix = 0U;
    const auto prefix_text = cidr.substr(slash + 1U);
    const auto result = std::from_chars(prefix_text.data(),
                                        prefix_text.data() + prefix_text.size(),
                                        prefix,
                                        10);
    if (result.ec != std::errc{} || result.ptr != prefix_text.data() + prefix_text.size() ||
        prefix > 32U)
    {
        throw std::invalid_argument("route prefix is out of range");
    }
    return Ipv4Route{core::parse_ipv4_address(cidr.substr(0U, slash)),
                     static_cast<std::uint8_t>(prefix),
                     gateway};
}

Ipv6Route parse_route_v6(const std::string& cidr, const core::Ipv6Address& gateway)
{
    const auto slash = cidr.find('/');
    if (slash == std::string::npos || slash == 0U || slash + 1U >= cidr.size())
    {
        throw std::invalid_argument("route must use IPv6 CIDR notation");
    }
    std::uint16_t prefix = 0U;
    const auto prefix_text = cidr.substr(slash + 1U);
    const auto result = std::from_chars(prefix_text.data(), prefix_text.data() + prefix_text.size(), prefix);
    if (result.ec != std::errc{} || result.ptr != prefix_text.data() + prefix_text.size() || prefix > 128U)
    {
        throw std::invalid_argument("IPv6 route prefix is out of range");
    }
    auto network = core::parse_ipv6_address(cidr.substr(0U, slash));
    auto canonical = network;
    for (std::size_t bit = prefix; bit < 128U; ++bit)
    {
        canonical[bit / 8U] = static_cast<std::uint8_t>(
            canonical[bit / 8U] & static_cast<std::uint8_t>(~(1U << (7U - bit % 8U))));
    }
    if (network != canonical)
    {
        throw std::invalid_argument("IPv6 route address must be canonical network address");
    }
    return Ipv6Route{network, static_cast<std::uint8_t>(prefix), gateway};
}

} // namespace

ClientNetworkTransaction::ClientNetworkTransaction(ClientNetworkBackend& backend) noexcept
    : backend_(backend)
{
}

ClientNetworkTransaction::~ClientNetworkTransaction()
{
    rollback();
}

void ClientNetworkTransaction::activate(const protocol::IpAssignment& assignment,
                                        const std::vector<std::string>& route_cidrs)
{
    if (active_ || adapter_open_ || session_started_ || address_created_ ||
        ipv6_address_created_ || !created_routes_.empty())
    {
        throw std::logic_error("network transaction is already active");
    }
    if (route_cidrs.empty())
    {
        throw std::invalid_argument("at least one tunnel route is required");
    }

    address_ = InterfaceAddress{assignment.client_address, assignment.prefix_length};
    ipv6_address_.reset();
    if (assignment.ipv6_prefix_length != 0U)
    {
        ipv6_address_ = Ipv6InterfaceAddress{assignment.client_ipv6_address,
                                             assignment.ipv6_prefix_length};
    }
    try
    {
        backend_.open_adapter();
        adapter_open_ = true;
        backend_.start_packet_session();
        session_started_ = true;
        address_created_ = backend_.add_interface_address(address_);
        if (ipv6_address_.has_value())
        {
            ipv6_address_created_ = backend_.add_interface_address(*ipv6_address_);
        }
        prior_mtu_ = backend_.set_interface_mtu(assignment.mtu);
        for (const auto& cidr : route_cidrs)
        {
            if (cidr.find(':') != std::string::npos)
            {
                const auto route = parse_route_v6(cidr, assignment.gateway_ipv6_address);
                if (backend_.add_route(route))
                {
                    created_routes_.emplace_back(route);
                }
            }
            else
            {
                const auto route = parse_route(cidr, assignment.gateway_address);
                if (backend_.add_route(route))
                {
                    created_routes_.emplace_back(route);
                }
            }
        }
        active_ = true;
    }
    catch (...)
    {
        rollback();
        throw;
    }
}

void ClientNetworkTransaction::rollback() noexcept
{
    active_ = false;
    for (auto route = created_routes_.rbegin(); route != created_routes_.rend(); ++route)
    {
        std::visit([this](const auto& value) { backend_.remove_route(value); }, *route);
    }
    created_routes_.clear();
    if (prior_mtu_.has_value())
    {
        backend_.restore_interface_mtu(*prior_mtu_);
        prior_mtu_.reset();
    }
    if (ipv6_address_created_ && ipv6_address_.has_value())
    {
        backend_.remove_interface_address(*ipv6_address_);
        ipv6_address_created_ = false;
    }
    if (address_created_)
    {
        backend_.remove_interface_address(address_);
        address_created_ = false;
    }
    if (session_started_)
    {
        backend_.stop_packet_session();
        session_started_ = false;
    }
    if (adapter_open_)
    {
        backend_.close_adapter();
        adapter_open_ = false;
    }
    ipv6_address_.reset();
}

} // namespace personal_vpn::client
