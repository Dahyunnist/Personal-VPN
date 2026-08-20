#ifndef PERSONAL_VPN_LEASE_MANAGER_HPP
#define PERSONAL_VPN_LEASE_MANAGER_HPP

#include "personal_vpn/control_messages.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace personal_vpn::core
{

using Ipv4Address = std::array<std::uint8_t, 4U>;

[[nodiscard]] Ipv4Address parse_ipv4_address(const std::string& text);
[[nodiscard]] std::string format_ipv4_address(const Ipv4Address& address);

struct Lease
{
    std::string identity;
    Ipv4Address address{};
    std::chrono::steady_clock::time_point expires_at{};

    [[nodiscard]] bool operator==(const Lease& other) const noexcept;
};

class LeaseManager
{
   public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    LeaseManager(Ipv4Address first_address,
                 Ipv4Address last_address,
                 Ipv4Address gateway_address,
                 std::uint8_t prefix_length,
                 std::uint16_t mtu,
                 std::chrono::seconds lease_duration);

    [[nodiscard]] Lease acquire(const std::string& identity, TimePoint now = Clock::now());
    [[nodiscard]] std::optional<Lease> find_by_identity(const std::string& identity,
                                                        TimePoint now = Clock::now());
    [[nodiscard]] bool owns(const std::string& identity,
                            const Ipv4Address& address,
                            TimePoint now = Clock::now());
    bool release(const std::string& identity);
    [[nodiscard]] std::size_t reap_expired(TimePoint now = Clock::now());

    [[nodiscard]] std::size_t active_count(TimePoint now = Clock::now());
    [[nodiscard]] std::size_t available_count(TimePoint now = Clock::now());
    [[nodiscard]] protocol::IpAssignment to_assignment(const Lease& lease) const;

   private:
    [[nodiscard]] static std::uint32_t to_integer(const Ipv4Address& address) noexcept;
    [[nodiscard]] static Ipv4Address from_integer(std::uint32_t address) noexcept;
    std::size_t reap_expired_locked(TimePoint now);

    const std::uint32_t first_address_;
    const std::uint32_t last_address_;
    const Ipv4Address gateway_address_;
    const std::uint8_t prefix_length_;
    const std::uint16_t mtu_;
    const std::chrono::seconds lease_duration_;

    std::mutex mutex_;
    std::unordered_map<std::string, Lease> leases_by_identity_;
    std::unordered_map<std::uint32_t, std::string> identity_by_address_;
};

} // namespace personal_vpn::core

#endif
