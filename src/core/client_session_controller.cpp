#include "personal_vpn/client_session_controller.hpp"

#include <stdexcept>
#include <limits>
#include <string>
#include <utility>

namespace personal_vpn::core
{
namespace
{

std::uint32_t address_value(const std::array<std::uint8_t, 4U>& address) noexcept
{
    return (static_cast<std::uint32_t>(address[0]) << 24U) |
           (static_cast<std::uint32_t>(address[1]) << 16U) |
           (static_cast<std::uint32_t>(address[2]) << 8U) |
           static_cast<std::uint32_t>(address[3]);
}

} // namespace

ClientSessionController::ClientSessionController(const std::uint16_t requested_mtu)
    : requested_mtu_(requested_mtu)
{
    static_cast<void>(protocol::encode_client_hello(
        protocol::ClientHello{requested_mtu_, protocol::kCapabilityIpv4}));
}

protocol::Frame ClientSessionController::start()
{
    if (state_ != ClientSessionState::Idle)
    {
        throw std::logic_error("client session has already started");
    }
    state_ = ClientSessionState::AwaitingAssignment;
    return make_outbound(
        protocol::MessageType::ClientHello,
        protocol::encode_client_hello(
            protocol::ClientHello{requested_mtu_, protocol::kCapabilityIpv4}));
}

ClientSessionResult ClientSessionController::handle(const protocol::Frame& frame)
{
    if (state_ == ClientSessionState::Idle || state_ == ClientSessionState::Closing ||
        state_ == ClientSessionState::Closed)
    {
        return ClientSessionResult{{}, {}, {}, {}, true};
    }
    if (frame.sequence != next_expected_sequence_)
    {
        return fail(ClientSessionErrorCode::InvalidSequence,
                    "unexpected inbound sequence; expected " +
                        std::to_string(next_expected_sequence_));
    }
    ++next_expected_sequence_;

    if (state_ == ClientSessionState::AwaitingAssignment)
    {
        if (frame.type != protocol::MessageType::IpAssign)
        {
            return fail(ClientSessionErrorCode::UnexpectedMessage,
                        "IP_ASSIGN must be the first server message");
        }
        try
        {
            const auto assignment = protocol::decode_ip_assignment(frame.payload);
            if (!assignment_is_acceptable(assignment))
            {
                return fail(ClientSessionErrorCode::InvalidAssignment,
                            "server IP assignment violates client tunnel policy");
            }
            assignment_ = assignment;
            state_ = ClientSessionState::Established;
            ClientSessionResult result;
            result.assignment = assignment;
            return result;
        }
        catch (const std::invalid_argument& error)
        {
            return fail(ClientSessionErrorCode::InvalidControlPayload, error.what());
        }
    }

    switch (frame.type)
    {
        case protocol::MessageType::DataIpv4:
        {
            ClientSessionErrorCode error_code = ClientSessionErrorCode::InvalidIpv4Packet;
            std::string error_message;
            if (!validate_ipv4_packet(frame.payload, true, error_code, error_message))
            {
                return fail(error_code, error_message);
            }
            ClientSessionResult result;
            result.packets_to_tun.push_back(frame.payload);
            return result;
        }
        case protocol::MessageType::Ping:
        {
            try
            {
                const auto probe = protocol::decode_liveness_probe(frame.payload);
                ClientSessionResult result;
                result.outbound_frames.push_back(
                    make_outbound(protocol::MessageType::Pong,
                                  protocol::encode_liveness_probe(probe)));
                return result;
            }
            catch (const std::invalid_argument& error)
            {
                return fail(ClientSessionErrorCode::InvalidControlPayload, error.what());
            }
        }
        case protocol::MessageType::Pong:
        {
            try
            {
                const auto probe = protocol::decode_liveness_probe(frame.payload);
                if (!pending_ping_nonce_ || probe.nonce != *pending_ping_nonce_)
                {
                    return fail(ClientSessionErrorCode::InvalidControlPayload,
                                "PONG nonce does not match an outstanding PING");
                }
                pending_ping_nonce_.reset();
                return {};
            }
            catch (const std::invalid_argument& error)
            {
                return fail(ClientSessionErrorCode::InvalidControlPayload, error.what());
            }
        }
        case protocol::MessageType::Error:
        {
            try
            {
                ClientSessionResult result;
                result.remote_error = protocol::decode_error_message(frame.payload);
                result.close_transport = true;
                state_ = ClientSessionState::Closed;
                return result;
            }
            catch (const std::invalid_argument& error)
            {
                return fail(ClientSessionErrorCode::InvalidControlPayload, error.what());
            }
        }
        case protocol::MessageType::Close:
        {
            try
            {
                static_cast<void>(protocol::decode_close_message(frame.payload));
                state_ = ClientSessionState::Closed;
                return ClientSessionResult{{}, {}, {}, {}, true};
            }
            catch (const std::invalid_argument& error)
            {
                return fail(ClientSessionErrorCode::InvalidControlPayload, error.what());
            }
        }
        case protocol::MessageType::ClientHello:
        case protocol::MessageType::IpAssign:
            return fail(ClientSessionErrorCode::UnexpectedMessage,
                        "message type is not valid in the established client state");
    }
    return fail(ClientSessionErrorCode::UnexpectedMessage, "unhandled server message type");
}

std::optional<protocol::Frame> ClientSessionController::make_data_to_server(
    const std::vector<std::uint8_t>& packet)
{
    if (state_ != ClientSessionState::Established)
    {
        return std::nullopt;
    }
    ClientSessionErrorCode error_code = ClientSessionErrorCode::InvalidIpv4Packet;
    std::string error_message;
    if (!validate_ipv4_packet(packet, false, error_code, error_message))
    {
        return std::nullopt;
    }
    return make_outbound(protocol::MessageType::DataIpv4, packet);
}

std::optional<protocol::Frame> ClientSessionController::make_ping(const std::uint64_t nonce)
{
    if (state_ != ClientSessionState::Established)
    {
        return std::nullopt;
    }
    if (pending_ping_nonce_)
    {
        return std::nullopt;
    }
    pending_ping_nonce_ = nonce;
    return make_outbound(protocol::MessageType::Ping,
                         protocol::encode_liveness_probe(protocol::LivenessProbe{nonce}));
}

std::optional<protocol::Frame> ClientSessionController::make_close(const std::uint16_t code)
{
    if (state_ != ClientSessionState::AwaitingAssignment &&
        state_ != ClientSessionState::Established)
    {
        return std::nullopt;
    }
    state_ = ClientSessionState::Closing;
    return make_outbound(protocol::MessageType::Close,
                         protocol::encode_close_message(protocol::CloseMessage{code}));
}

void ClientSessionController::on_transport_closed() noexcept
{
    state_ = ClientSessionState::Closed;
}

ClientSessionResult ClientSessionController::fail(const ClientSessionErrorCode code,
                                                  const std::string& message)
{
    ClientSessionResult result;
    result.outbound_frames.push_back(
        make_outbound(protocol::MessageType::Error,
                      protocol::encode_error_message(protocol::ErrorMessage{
                          static_cast<std::uint16_t>(code), message})));
    result.close_transport = true;
    state_ = ClientSessionState::Closing;
    return result;
}

protocol::Frame ClientSessionController::make_outbound(const protocol::MessageType type,
                                                       std::vector<std::uint8_t> payload)
{
    return protocol::Frame{type, 0U, next_outbound_sequence_++, std::move(payload)};
}

bool ClientSessionController::assignment_is_acceptable(
    const protocol::IpAssignment& assignment) const
{
    if (assignment.mtu > requested_mtu_ || assignment.client_address == assignment.gateway_address)
    {
        return false;
    }
    const auto mask = assignment.prefix_length == 32U
                          ? std::numeric_limits<std::uint32_t>::max()
                          : std::numeric_limits<std::uint32_t>::max()
                                << (32U - assignment.prefix_length);
    const auto client = address_value(assignment.client_address);
    const auto gateway = address_value(assignment.gateway_address);
    if ((client & mask) != (gateway & mask))
    {
        return false;
    }
    if (assignment.prefix_length <= 30U)
    {
        const auto network = gateway & mask;
        const auto broadcast = network | ~mask;
        if (client == network || client == broadcast || gateway == network ||
            gateway == broadcast)
        {
            return false;
        }
    }
    return true;
}

bool ClientSessionController::validate_ipv4_packet(
    const std::vector<std::uint8_t>& packet,
    const bool from_server,
    ClientSessionErrorCode& error_code,
    std::string& error_message) const
{
    if (!assignment_ || packet.size() < 20U || packet.size() > assignment_->mtu)
    {
        error_message = "IPv4 packet size is outside the negotiated tunnel policy";
        return false;
    }
    const auto version = static_cast<std::uint8_t>(packet[0] >> 4U);
    const auto header_length = static_cast<std::size_t>(packet[0] & 0x0FU) * 4U;
    const auto total_length = static_cast<std::size_t>(
        (static_cast<std::uint16_t>(packet[2]) << 8U) |
        static_cast<std::uint16_t>(packet[3]));
    if (version != 4U || header_length < 20U || header_length > packet.size() ||
        total_length != packet.size())
    {
        error_message = "IPv4 header or total length is invalid";
        return false;
    }
    const std::array<std::uint8_t, 4U> packet_address =
        from_server
            ? std::array<std::uint8_t, 4U>{packet[16], packet[17], packet[18], packet[19]}
            : std::array<std::uint8_t, 4U>{packet[12], packet[13], packet[14], packet[15]};
    if (packet_address != assignment_->client_address)
    {
        error_code = ClientSessionErrorCode::AddressMismatch;
        error_message = from_server
                            ? "server IPv4 destination does not match the assigned client address"
                            : "local IPv4 source does not match the assigned client address";
        return false;
    }
    return true;
}

} // namespace personal_vpn::core
