#ifndef PERSONAL_VPN_CLIENT_NETWORK_TRANSACTION_HPP
#define PERSONAL_VPN_CLIENT_NETWORK_TRANSACTION_HPP

#include "personal_vpn/control_messages.hpp"
#include "personal_vpn/lease_manager.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace personal_vpn::client
{

struct InterfaceAddress
{
    core::Ipv4Address address{};
    std::uint8_t prefix_length{0U};

    [[nodiscard]] bool operator==(const InterfaceAddress& other) const noexcept
    {
        return address == other.address && prefix_length == other.prefix_length;
    }
};

struct Ipv4Route
{
    core::Ipv4Address network{};
    std::uint8_t prefix_length{0U};
    core::Ipv4Address gateway{};

    [[nodiscard]] bool operator==(const Ipv4Route& other) const noexcept
    {
        return network == other.network && prefix_length == other.prefix_length &&
               gateway == other.gateway;
    }
};

struct Ipv6InterfaceAddress
{
    core::Ipv6Address address{};
    std::uint8_t prefix_length{0U};
    [[nodiscard]] bool operator==(const Ipv6InterfaceAddress& other) const noexcept
    {
        return address == other.address && prefix_length == other.prefix_length;
    }
};

struct Ipv6Route
{
    core::Ipv6Address network{};
    std::uint8_t prefix_length{0U};
    core::Ipv6Address gateway{};
    [[nodiscard]] bool operator==(const Ipv6Route& other) const noexcept
    {
        return network == other.network && prefix_length == other.prefix_length &&
               gateway == other.gateway;
    }
};

struct InterfaceMtuSnapshot
{
    std::uint32_t ipv4{0U};
    std::uint32_t ipv6{0U};
};

class ClientNetworkBackend
{
   public:
    virtual ~ClientNetworkBackend() = default;

    // A throwing setup method must release any resource it acquired before throwing.
    virtual void open_adapter() = 0;
    virtual void start_packet_session() = 0;
    [[nodiscard]] virtual bool add_interface_address(const InterfaceAddress& address) = 0;
    [[nodiscard]] virtual std::optional<InterfaceMtuSnapshot> set_interface_mtu(
        std::uint32_t mtu) = 0;
    [[nodiscard]] virtual bool add_route(const Ipv4Route& route) = 0;
    [[nodiscard]] virtual bool add_interface_address(const Ipv6InterfaceAddress& address)
    {
        static_cast<void>(address);
        throw std::runtime_error("IPv6 interface addressing is unsupported by this backend");
    }
    [[nodiscard]] virtual bool add_route(const Ipv6Route& route)
    {
        static_cast<void>(route);
        throw std::runtime_error("IPv6 routes are unsupported by this backend");
    }

    virtual void remove_route(const Ipv4Route& route) noexcept = 0;
    virtual void remove_route(const Ipv6Route& route) noexcept { static_cast<void>(route); }
    virtual void restore_interface_mtu(const InterfaceMtuSnapshot& mtu) noexcept = 0;
    virtual void remove_interface_address(const InterfaceAddress& address) noexcept = 0;
    virtual void remove_interface_address(const Ipv6InterfaceAddress& address) noexcept
    {
        static_cast<void>(address);
    }
    virtual void stop_packet_session() noexcept = 0;
    virtual void close_adapter() noexcept = 0;
};

class ClientPacketDevice : public ClientNetworkBackend
{
   public:
    ~ClientPacketDevice() override = default;

    // Blocks until one packet is available or interrupt_receive() is called.
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> receive_packet() = 0;
    virtual void interrupt_receive() noexcept = 0;
    virtual void send_packet(const std::vector<std::uint8_t>& packet) = 0;
};

class ClientNetworkTransaction final
{
   public:
    explicit ClientNetworkTransaction(ClientNetworkBackend& backend) noexcept;
    ~ClientNetworkTransaction();

    ClientNetworkTransaction(const ClientNetworkTransaction&) = delete;
    ClientNetworkTransaction& operator=(const ClientNetworkTransaction&) = delete;

    void activate(const protocol::IpAssignment& assignment,
                  const std::vector<std::string>& route_cidrs);
    void rollback() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }

   private:
    ClientNetworkBackend& backend_;
    InterfaceAddress address_{};
    std::optional<Ipv6InterfaceAddress> ipv6_address_;
    std::vector<std::variant<Ipv4Route, Ipv6Route>> created_routes_;
    std::optional<InterfaceMtuSnapshot> prior_mtu_;
    bool adapter_open_{false};
    bool session_started_{false};
    bool address_created_{false};
    bool ipv6_address_created_{false};
    bool active_{false};
};

} // namespace personal_vpn::client

#endif
