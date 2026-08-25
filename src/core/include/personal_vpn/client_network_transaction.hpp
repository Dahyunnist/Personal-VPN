#ifndef PERSONAL_VPN_CLIENT_NETWORK_TRANSACTION_HPP
#define PERSONAL_VPN_CLIENT_NETWORK_TRANSACTION_HPP

#include "personal_vpn/control_messages.hpp"
#include "personal_vpn/lease_manager.hpp"

#include <cstdint>
#include <optional>
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

class ClientNetworkBackend
{
   public:
    virtual ~ClientNetworkBackend() = default;

    // A throwing setup method must release any resource it acquired before throwing.
    virtual void open_adapter() = 0;
    virtual void start_packet_session() = 0;
    [[nodiscard]] virtual bool add_interface_address(const InterfaceAddress& address) = 0;
    [[nodiscard]] virtual std::optional<std::uint32_t> set_interface_mtu(
        std::uint32_t mtu) = 0;
    [[nodiscard]] virtual bool add_route(const Ipv4Route& route) = 0;

    virtual void remove_route(const Ipv4Route& route) noexcept = 0;
    virtual void restore_interface_mtu(std::uint32_t mtu) noexcept = 0;
    virtual void remove_interface_address(const InterfaceAddress& address) noexcept = 0;
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
    std::vector<Ipv4Route> created_routes_;
    std::optional<std::uint32_t> prior_mtu_;
    bool adapter_open_{false};
    bool session_started_{false};
    bool address_created_{false};
    bool active_{false};
};

} // namespace personal_vpn::client

#endif
