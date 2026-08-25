#ifndef PERSONAL_VPN_SESSION_CONTROLLER_HPP
#define PERSONAL_VPN_SESSION_CONTROLLER_HPP

#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/protocol.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace personal_vpn::core
{

enum class SessionState
{
    AwaitingHello,
    Established,
    Closing,
    Closed,
};

enum class SessionErrorCode : std::uint16_t
{
    UnexpectedMessage = 1U,
    InvalidSequence = 2U,
    InvalidControlPayload = 3U,
    LeaseUnavailable = 4U,
    InvalidIpv4Packet = 5U,
    SourceAddressMismatch = 6U,
};

struct SessionResult
{
    std::vector<protocol::Frame> outbound_frames;
    std::vector<std::vector<std::uint8_t>> packets_to_tun;
    bool close_transport{false};
};

class SessionController
{
   public:
    SessionController(LeaseManager& lease_manager, std::string authenticated_identity);
    ~SessionController();

    SessionController(const SessionController&) = delete;
    SessionController& operator=(const SessionController&) = delete;

    [[nodiscard]] SessionResult handle(const protocol::Frame& frame,
                                       LeaseManager::TimePoint now = LeaseManager::Clock::now());
    [[nodiscard]] std::optional<protocol::Frame> make_data_to_client(
        const std::vector<std::uint8_t>& packet,
        LeaseManager::TimePoint now = LeaseManager::Clock::now());
    void on_transport_closed() noexcept;

    [[nodiscard]] SessionState state() const noexcept { return state_; }
    [[nodiscard]] const Lease* lease() const noexcept { return lease_.get(); }
    [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept
    {
        return next_expected_sequence_;
    }

   private:
    [[nodiscard]] SessionResult fail(SessionErrorCode code, const std::string& message);
    [[nodiscard]] protocol::Frame make_outbound(protocol::MessageType type,
                                                std::vector<std::uint8_t> payload);
    [[nodiscard]] bool validate_ipv4_packet(const std::vector<std::uint8_t>& packet,
                                            SessionErrorCode& error_code,
                                            std::string& error_message) const;
    void release_lease() noexcept;

    LeaseManager& lease_manager_;
    std::string identity_;
    SessionState state_{SessionState::AwaitingHello};
    std::unique_ptr<Lease> lease_;
    std::uint16_t negotiated_mtu_{1'400U};
    std::uint64_t next_expected_sequence_{1U};
    std::uint64_t next_outbound_sequence_{1U};
};

} // namespace personal_vpn::core

#endif
