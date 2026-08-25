#ifndef PERSONAL_VPN_CLIENT_SESSION_CONTROLLER_HPP
#define PERSONAL_VPN_CLIENT_SESSION_CONTROLLER_HPP

#include "personal_vpn/control_messages.hpp"
#include "personal_vpn/protocol.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace personal_vpn::core
{

enum class ClientSessionState
{
    Idle,
    AwaitingAssignment,
    Established,
    Closing,
    Closed,
};

enum class ClientSessionErrorCode : std::uint16_t
{
    UnexpectedMessage = 101U,
    InvalidSequence = 102U,
    InvalidControlPayload = 103U,
    InvalidAssignment = 104U,
    InvalidIpv4Packet = 105U,
    AddressMismatch = 106U,
};

struct ClientSessionResult
{
    std::vector<protocol::Frame> outbound_frames;
    std::vector<std::vector<std::uint8_t>> packets_to_tun;
    std::optional<protocol::IpAssignment> assignment;
    std::optional<protocol::ErrorMessage> remote_error;
    bool close_transport{false};
};

class ClientSessionController
{
   public:
    explicit ClientSessionController(std::uint16_t requested_mtu = 1'400U);

    [[nodiscard]] protocol::Frame start();
    [[nodiscard]] ClientSessionResult handle(const protocol::Frame& frame);
    [[nodiscard]] std::optional<protocol::Frame> make_data_to_server(
        const std::vector<std::uint8_t>& packet);
    [[nodiscard]] std::optional<protocol::Frame> make_ping(std::uint64_t nonce);
    [[nodiscard]] std::optional<protocol::Frame> make_close(std::uint16_t code = 0U);
    void on_transport_closed() noexcept;

    [[nodiscard]] ClientSessionState state() const noexcept { return state_; }
    [[nodiscard]] const std::optional<protocol::IpAssignment>& assignment() const noexcept
    {
        return assignment_;
    }
    [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept
    {
        return next_expected_sequence_;
    }

   private:
    [[nodiscard]] ClientSessionResult fail(ClientSessionErrorCode code,
                                           const std::string& message);
    [[nodiscard]] protocol::Frame make_outbound(protocol::MessageType type,
                                                std::vector<std::uint8_t> payload);
    [[nodiscard]] bool assignment_is_acceptable(const protocol::IpAssignment& assignment) const;
    [[nodiscard]] bool validate_ipv4_packet(const std::vector<std::uint8_t>& packet,
                                            bool from_server,
                                            ClientSessionErrorCode& error_code,
                                            std::string& error_message) const;

    const std::uint16_t requested_mtu_;
    ClientSessionState state_{ClientSessionState::Idle};
    std::optional<protocol::IpAssignment> assignment_;
    std::optional<std::uint64_t> pending_ping_nonce_;
    std::uint64_t next_expected_sequence_{1U};
    std::uint64_t next_outbound_sequence_{1U};
};

} // namespace personal_vpn::core

#endif
