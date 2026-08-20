#ifndef PERSONAL_VPN_CONTROL_MESSAGES_HPP
#define PERSONAL_VPN_CONTROL_MESSAGES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace personal_vpn::protocol
{

constexpr std::uint16_t kMinimumIpv4Mtu = 576U;
constexpr std::uint16_t kMaximumTunnelMtu = 9'000U;
constexpr std::uint32_t kCapabilityIpv4 = 1U << 0U;
constexpr std::size_t kMaximumErrorMessageSize = 1'024U;

struct ClientHello
{
    std::uint16_t requested_mtu{1'400U};
    std::uint32_t capabilities{kCapabilityIpv4};

    [[nodiscard]] bool operator==(const ClientHello& other) const noexcept;
};

struct IpAssignment
{
    std::array<std::uint8_t, 4U> client_address{};
    std::array<std::uint8_t, 4U> gateway_address{};
    std::uint8_t prefix_length{24U};
    std::uint16_t mtu{1'400U};
    std::uint32_t lease_seconds{3'600U};

    [[nodiscard]] bool operator==(const IpAssignment& other) const noexcept;
};

struct LivenessProbe
{
    std::uint64_t nonce{0U};

    [[nodiscard]] bool operator==(const LivenessProbe& other) const noexcept;
};

struct ErrorMessage
{
    std::uint16_t code{0U};
    std::string message;

    [[nodiscard]] bool operator==(const ErrorMessage& other) const noexcept;
};

struct CloseMessage
{
    std::uint16_t code{0U};

    [[nodiscard]] bool operator==(const CloseMessage& other) const noexcept;
};

[[nodiscard]] std::vector<std::uint8_t> encode_client_hello(const ClientHello& message);
[[nodiscard]] ClientHello decode_client_hello(const std::vector<std::uint8_t>& payload);

[[nodiscard]] std::vector<std::uint8_t> encode_ip_assignment(const IpAssignment& message);
[[nodiscard]] IpAssignment decode_ip_assignment(const std::vector<std::uint8_t>& payload);

[[nodiscard]] std::vector<std::uint8_t> encode_liveness_probe(const LivenessProbe& message);
[[nodiscard]] LivenessProbe decode_liveness_probe(const std::vector<std::uint8_t>& payload);

[[nodiscard]] std::vector<std::uint8_t> encode_error_message(const ErrorMessage& message);
[[nodiscard]] ErrorMessage decode_error_message(const std::vector<std::uint8_t>& payload);

[[nodiscard]] std::vector<std::uint8_t> encode_close_message(const CloseMessage& message);
[[nodiscard]] CloseMessage decode_close_message(const std::vector<std::uint8_t>& payload);

} // namespace personal_vpn::protocol

#endif
