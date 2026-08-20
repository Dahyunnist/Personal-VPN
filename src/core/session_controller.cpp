#include "personal_vpn/session_controller.hpp"

#include "personal_vpn/control_messages.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace personal_vpn::core
{

SessionController::SessionController(LeaseManager& lease_manager, std::string authenticated_identity)
    : lease_manager_(lease_manager), identity_(std::move(authenticated_identity))
{
    if (identity_.empty())
    {
        throw std::invalid_argument("authenticated session identity must not be empty");
    }
}

SessionController::~SessionController()
{
    release_lease();
}

SessionResult SessionController::handle(const protocol::Frame& frame,
                                        const LeaseManager::TimePoint now)
{
    if (state_ == SessionState::Closing || state_ == SessionState::Closed)
    {
        return SessionResult{{}, {}, true};
    }
    if (frame.sequence != next_expected_sequence_)
    {
        return fail(SessionErrorCode::InvalidSequence,
                    "unexpected inbound sequence; expected " +
                        std::to_string(next_expected_sequence_));
    }
    ++next_expected_sequence_;

    if (state_ == SessionState::AwaitingHello)
    {
        if (frame.type != protocol::MessageType::ClientHello)
        {
            return fail(SessionErrorCode::UnexpectedMessage,
                        "CLIENT_HELLO must be the first tunnel message");
        }
        try
        {
            const auto hello = protocol::decode_client_hello(frame.payload);
            lease_ = std::make_unique<Lease>(lease_manager_.acquire(identity_, now));
            auto assignment = lease_manager_.to_assignment(*lease_);
            negotiated_mtu_ = std::min(hello.requested_mtu, assignment.mtu);
            assignment.mtu = negotiated_mtu_;
            state_ = SessionState::Established;
            SessionResult result;
            result.outbound_frames.push_back(
                make_outbound(protocol::MessageType::IpAssign,
                              protocol::encode_ip_assignment(assignment)));
            return result;
        }
        catch (const std::invalid_argument& error)
        {
            return fail(SessionErrorCode::InvalidControlPayload, error.what());
        }
        catch (const std::runtime_error& error)
        {
            return fail(SessionErrorCode::LeaseUnavailable, error.what());
        }
    }

    if (frame.type == protocol::MessageType::DataIpv4 || frame.type == protocol::MessageType::Ping)
    {
        const auto renewed = lease_manager_.renew(identity_, lease_->address, now);
        if (!renewed.has_value())
        {
            return fail(SessionErrorCode::LeaseUnavailable,
                        "authenticated virtual IP lease has expired or changed");
        }
        *lease_ = *renewed;
    }

    switch (frame.type)
    {
        case protocol::MessageType::DataIpv4:
        {
            SessionErrorCode error_code = SessionErrorCode::InvalidIpv4Packet;
            std::string error_message;
            if (!validate_ipv4_packet(frame.payload, error_code, error_message))
            {
                return fail(error_code, error_message);
            }
            SessionResult result;
            result.packets_to_tun.push_back(frame.payload);
            return result;
        }
        case protocol::MessageType::Ping:
        {
            try
            {
                const auto probe = protocol::decode_liveness_probe(frame.payload);
                SessionResult result;
                result.outbound_frames.push_back(
                    make_outbound(protocol::MessageType::Pong,
                                  protocol::encode_liveness_probe(probe)));
                return result;
            }
            catch (const std::invalid_argument& error)
            {
                return fail(SessionErrorCode::InvalidControlPayload, error.what());
            }
        }
        case protocol::MessageType::Close:
        {
            try
            {
                static_cast<void>(protocol::decode_close_message(frame.payload));
            }
            catch (const std::invalid_argument& error)
            {
                return fail(SessionErrorCode::InvalidControlPayload, error.what());
            }
            state_ = SessionState::Closed;
            release_lease();
            return SessionResult{{}, {}, true};
        }
        case protocol::MessageType::Error:
        {
            try
            {
                static_cast<void>(protocol::decode_error_message(frame.payload));
            }
            catch (const std::invalid_argument& error)
            {
                return fail(SessionErrorCode::InvalidControlPayload, error.what());
            }
            state_ = SessionState::Closed;
            release_lease();
            return SessionResult{{}, {}, true};
        }
        case protocol::MessageType::ClientHello:
        case protocol::MessageType::IpAssign:
        case protocol::MessageType::Pong:
            return fail(SessionErrorCode::UnexpectedMessage,
                        "message type is not valid in this server session state");
    }

    return fail(SessionErrorCode::UnexpectedMessage, "unhandled tunnel message type");
}

std::optional<protocol::Frame> SessionController::make_data_to_client(
    const std::vector<std::uint8_t>& packet,
    const LeaseManager::TimePoint now)
{
    if (state_ != SessionState::Established || !lease_ || packet.size() < 20U ||
        packet.size() > negotiated_mtu_)
    {
        return std::nullopt;
    }
    const auto version = static_cast<std::uint8_t>(packet[0] >> 4U);
    const auto header_length = static_cast<std::size_t>(packet[0] & 0x0FU) * 4U;
    const auto total_length = static_cast<std::size_t>(
        (static_cast<std::uint16_t>(packet[2]) << 8U) | static_cast<std::uint16_t>(packet[3]));
    const Ipv4Address destination{packet[16], packet[17], packet[18], packet[19]};
    if (version != 4U || header_length < 20U || header_length > packet.size() ||
        total_length != packet.size() || destination != lease_->address)
    {
        return std::nullopt;
    }
    const auto renewed = lease_manager_.renew(identity_, lease_->address, now);
    if (!renewed.has_value())
    {
        state_ = SessionState::Closing;
        release_lease();
        return std::nullopt;
    }
    *lease_ = *renewed;
    return make_outbound(protocol::MessageType::DataIpv4, packet);
}

void SessionController::on_transport_closed() noexcept
{
    state_ = SessionState::Closed;
    release_lease();
}

SessionResult SessionController::fail(const SessionErrorCode code, const std::string& message)
{
    SessionResult result;
    result.outbound_frames.push_back(
        make_outbound(protocol::MessageType::Error,
                      protocol::encode_error_message(
                          protocol::ErrorMessage{static_cast<std::uint16_t>(code), message})));
    result.close_transport = true;
    state_ = SessionState::Closing;
    release_lease();
    return result;
}

protocol::Frame SessionController::make_outbound(const protocol::MessageType type,
                                                 std::vector<std::uint8_t> payload)
{
    return protocol::Frame{type, 0U, next_outbound_sequence_++, std::move(payload)};
}

bool SessionController::validate_ipv4_packet(const std::vector<std::uint8_t>& packet,
                                             SessionErrorCode& error_code,
                                             std::string& error_message) const
{
    if (!lease_)
    {
        error_message = "session has no active virtual IP lease";
        return false;
    }
    if (packet.size() < 20U)
    {
        error_message = "IPv4 packet is shorter than the minimum header";
        return false;
    }
    if (packet.size() > negotiated_mtu_)
    {
        error_message = "IPv4 packet exceeds the negotiated tunnel MTU";
        return false;
    }
    const auto version = static_cast<std::uint8_t>(packet[0] >> 4U);
    const auto header_length = static_cast<std::size_t>(packet[0] & 0x0FU) * 4U;
    if (version != 4U || header_length < 20U || header_length > packet.size())
    {
        error_message = "IPv4 version or header length is invalid";
        return false;
    }
    const auto total_length = static_cast<std::size_t>(
        (static_cast<std::uint16_t>(packet[2]) << 8U) | static_cast<std::uint16_t>(packet[3]));
    if (total_length != packet.size())
    {
        error_message = "IPv4 total length does not match the tunnel frame";
        return false;
    }
    const Ipv4Address source{packet[12], packet[13], packet[14], packet[15]};
    if (source != lease_->address)
    {
        error_code = SessionErrorCode::SourceAddressMismatch;
        error_message = "IPv4 source address does not match the authenticated lease";
        return false;
    }
    return true;
}

void SessionController::release_lease() noexcept
{
    if (lease_)
    {
        static_cast<void>(lease_manager_.release(identity_));
        lease_.reset();
    }
}

} // namespace personal_vpn::core
