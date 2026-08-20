#ifndef PERSONAL_VPN_WINDOWS_NETWORK_BACKEND_HPP
#define PERSONAL_VPN_WINDOWS_NETWORK_BACKEND_HPP

#include "personal_vpn/client_network_transaction.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace personal_vpn::client
{

class WindowsNetworkBackend final : public ClientNetworkBackend
{
   public:
    explicit WindowsNetworkBackend(std::wstring adapter_name = L"PersonalVPN");
    ~WindowsNetworkBackend() override;

    WindowsNetworkBackend(const WindowsNetworkBackend&) = delete;
    WindowsNetworkBackend& operator=(const WindowsNetworkBackend&) = delete;

    void open_adapter() override;
    void start_packet_session() override;
    [[nodiscard]] bool add_interface_address(const InterfaceAddress& address) override;
    [[nodiscard]] std::optional<std::uint32_t> set_interface_mtu(
        std::uint32_t mtu) override;
    [[nodiscard]] bool add_route(const Ipv4Route& route) override;
    void remove_route(const Ipv4Route& route) noexcept override;
    void restore_interface_mtu(std::uint32_t mtu) noexcept override;
    void remove_interface_address(const InterfaceAddress& address) noexcept override;
    void stop_packet_session() noexcept override;
    void close_adapter() noexcept override;

    [[nodiscard]] void* read_wait_handle() const noexcept;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> try_receive_packet();
    void send_packet(const std::vector<std::uint8_t>& packet);

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace personal_vpn::client

#endif
