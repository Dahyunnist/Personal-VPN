#include "personal_vpn/control_messages.hpp"

#include <algorithm>
#include <stdexcept>

namespace personal_vpn::protocol
{
namespace
{

void append_u16(std::vector<std::uint8_t>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64(std::vector<std::uint8_t>& output, const std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        output.push_back(static_cast<std::uint8_t>((value >> static_cast<unsigned int>(shift)) & 0xFFU));
    }
}

std::uint16_t read_u16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t read_u32(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64(const std::uint8_t* data)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        value = (value << 8U) | static_cast<std::uint64_t>(data[index]);
    }
    return value;
}

void validate_mtu(const std::uint16_t mtu)
{
    if (mtu < kMinimumIpv4Mtu || mtu > kMaximumTunnelMtu)
    {
        throw std::invalid_argument("tunnel MTU is outside the supported range");
    }
}

void require_size(const std::vector<std::uint8_t>& payload,
                  const std::size_t expected,
                  const char* message_name)
{
    if (payload.size() != expected)
    {
        throw std::invalid_argument(std::string(message_name) + " payload has an invalid length");
    }
}

bool is_unspecified(const std::array<std::uint8_t, 4U>& address)
{
    return address[0] == 0U && address[1] == 0U && address[2] == 0U && address[3] == 0U;
}

void validate_ip_assignment(const IpAssignment& message)
{
    if (is_unspecified(message.client_address) || is_unspecified(message.gateway_address))
    {
        throw std::invalid_argument("IP assignment cannot contain an unspecified address");
    }
    if (message.prefix_length == 0U || message.prefix_length > 32U)
    {
        throw std::invalid_argument("IPv4 prefix length must be between 1 and 32");
    }
    validate_mtu(message.mtu);
    if (message.lease_seconds == 0U)
    {
        throw std::invalid_argument("IP lease duration must be non-zero");
    }
}

} // namespace

bool ClientHello::operator==(const ClientHello& other) const noexcept
{
    return requested_mtu == other.requested_mtu && capabilities == other.capabilities;
}

bool IpAssignment::operator==(const IpAssignment& other) const noexcept
{
    return client_address == other.client_address && gateway_address == other.gateway_address &&
           prefix_length == other.prefix_length && mtu == other.mtu &&
           lease_seconds == other.lease_seconds;
}

bool LivenessProbe::operator==(const LivenessProbe& other) const noexcept
{
    return nonce == other.nonce;
}

bool ErrorMessage::operator==(const ErrorMessage& other) const noexcept
{
    return code == other.code && message == other.message;
}

bool CloseMessage::operator==(const CloseMessage& other) const noexcept
{
    return code == other.code;
}

std::vector<std::uint8_t> encode_client_hello(const ClientHello& message)
{
    validate_mtu(message.requested_mtu);
    if ((message.capabilities & kCapabilityIpv4) == 0U)
    {
        throw std::invalid_argument("client must advertise IPv4 tunnel support");
    }
    std::vector<std::uint8_t> output;
    output.reserve(8U);
    append_u16(output, message.requested_mtu);
    append_u16(output, 0U);
    append_u32(output, message.capabilities);
    return output;
}

ClientHello decode_client_hello(const std::vector<std::uint8_t>& payload)
{
    require_size(payload, 8U, "CLIENT_HELLO");
    if (read_u16(payload.data() + 2U) != 0U)
    {
        throw std::invalid_argument("CLIENT_HELLO reserved field must be zero");
    }
    ClientHello message{read_u16(payload.data()), read_u32(payload.data() + 4U)};
    static_cast<void>(encode_client_hello(message));
    return message;
}

std::vector<std::uint8_t> encode_ip_assignment(const IpAssignment& message)
{
    validate_ip_assignment(message);
    std::vector<std::uint8_t> output;
    output.reserve(16U);
    output.insert(output.end(), message.client_address.begin(), message.client_address.end());
    output.insert(output.end(), message.gateway_address.begin(), message.gateway_address.end());
    output.push_back(message.prefix_length);
    output.push_back(0U);
    append_u16(output, message.mtu);
    append_u32(output, message.lease_seconds);
    return output;
}

IpAssignment decode_ip_assignment(const std::vector<std::uint8_t>& payload)
{
    require_size(payload, 16U, "IP_ASSIGN");
    if (payload[9] != 0U)
    {
        throw std::invalid_argument("IP_ASSIGN reserved field must be zero");
    }
    IpAssignment message;
    std::copy_n(payload.begin(), 4U, message.client_address.begin());
    std::copy_n(payload.begin() + 4, 4U, message.gateway_address.begin());
    message.prefix_length = payload[8];
    message.mtu = read_u16(payload.data() + 10U);
    message.lease_seconds = read_u32(payload.data() + 12U);
    validate_ip_assignment(message);
    return message;
}

std::vector<std::uint8_t> encode_liveness_probe(const LivenessProbe& message)
{
    std::vector<std::uint8_t> output;
    output.reserve(8U);
    append_u64(output, message.nonce);
    return output;
}

LivenessProbe decode_liveness_probe(const std::vector<std::uint8_t>& payload)
{
    require_size(payload, 8U, "liveness probe");
    return LivenessProbe{read_u64(payload.data())};
}

std::vector<std::uint8_t> encode_error_message(const ErrorMessage& message)
{
    if (message.message.size() > kMaximumErrorMessageSize)
    {
        throw std::invalid_argument("ERROR message text exceeds the protocol limit");
    }
    std::vector<std::uint8_t> output;
    output.reserve(2U + message.message.size());
    append_u16(output, message.code);
    output.insert(output.end(), message.message.begin(), message.message.end());
    return output;
}

ErrorMessage decode_error_message(const std::vector<std::uint8_t>& payload)
{
    if (payload.size() < 2U || payload.size() > 2U + kMaximumErrorMessageSize)
    {
        throw std::invalid_argument("ERROR payload has an invalid length");
    }
    return ErrorMessage{read_u16(payload.data()), std::string(payload.begin() + 2, payload.end())};
}

std::vector<std::uint8_t> encode_close_message(const CloseMessage& message)
{
    std::vector<std::uint8_t> output;
    output.reserve(2U);
    append_u16(output, message.code);
    return output;
}

CloseMessage decode_close_message(const std::vector<std::uint8_t>& payload)
{
    require_size(payload, 2U, "CLOSE");
    return CloseMessage{read_u16(payload.data())};
}

} // namespace personal_vpn::protocol
