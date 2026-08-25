#include "personal_vpn/lease_manager.hpp"

#include <charconv>
#include <limits>
#include <stdexcept>

namespace personal_vpn::core
{
namespace
{

bool is_unspecified(const Ipv4Address& address) noexcept
{
    return address[0] == 0U && address[1] == 0U && address[2] == 0U && address[3] == 0U;
}

} // namespace

Ipv4Address parse_ipv4_address(const std::string& text)
{
    Ipv4Address address{};
    std::size_t begin = 0U;
    for (std::size_t component = 0U; component < address.size(); ++component)
    {
        const auto end = text.find('.', begin);
        const auto component_end = end == std::string::npos ? text.size() : end;
        if (component_end == begin)
        {
            throw std::invalid_argument("IPv4 address contains an empty component");
        }

        unsigned int value = 0U;
        const auto* first = text.data() + begin;
        const auto* last = text.data() + component_end;
        const auto result = std::from_chars(first, last, value, 10);
        if (result.ec != std::errc{} || result.ptr != last || value > 255U)
        {
            throw std::invalid_argument("IPv4 address contains an invalid component");
        }
        address[component] = static_cast<std::uint8_t>(value);

        if (component < address.size() - 1U)
        {
            if (end == std::string::npos)
            {
                throw std::invalid_argument("IPv4 address has too few components");
            }
            begin = end + 1U;
        }
        else if (end != std::string::npos)
        {
            throw std::invalid_argument("IPv4 address has too many components");
        }
    }
    return address;
}

std::string format_ipv4_address(const Ipv4Address& address)
{
    return std::to_string(address[0]) + "." + std::to_string(address[1]) + "." +
           std::to_string(address[2]) + "." + std::to_string(address[3]);
}

bool Lease::operator==(const Lease& other) const noexcept
{
    return lease_id == other.lease_id && identity == other.identity &&
           address == other.address && expires_at == other.expires_at;
}

LeaseManager::LeaseManager(const Ipv4Address first_address,
                           const Ipv4Address last_address,
                           const Ipv4Address gateway_address,
                           const std::uint8_t prefix_length,
                           const std::uint16_t mtu,
                           const std::chrono::seconds lease_duration)
    : first_address_(to_integer(first_address)),
      last_address_(to_integer(last_address)),
      gateway_address_(gateway_address),
      prefix_length_(prefix_length),
      mtu_(mtu),
      lease_duration_(lease_duration)
{
    if (first_address_ > last_address_)
    {
        throw std::invalid_argument("first lease address must not exceed the last address");
    }
    if (is_unspecified(gateway_address_))
    {
        throw std::invalid_argument("lease gateway must not be unspecified");
    }
    if (prefix_length_ == 0U || prefix_length_ > 32U)
    {
        throw std::invalid_argument("lease prefix length must be between 1 and 32");
    }
    if (mtu_ < protocol::kMinimumIpv4Mtu || mtu_ > protocol::kMaximumTunnelMtu)
    {
        throw std::invalid_argument("lease MTU is outside the supported range");
    }
    if (lease_duration_.count() <= 0 ||
        static_cast<std::uint64_t>(lease_duration_.count()) >
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        throw std::invalid_argument("lease duration is outside the protocol range");
    }

    const auto gateway = to_integer(gateway_address_);
    if (gateway >= first_address_ && gateway <= last_address_)
    {
        throw std::invalid_argument("lease pool must not contain the gateway address");
    }
    const auto mask = prefix_length_ == 32U
                          ? std::numeric_limits<std::uint32_t>::max()
                          : std::numeric_limits<std::uint32_t>::max() << (32U - prefix_length_);
    const auto network = gateway & mask;
    if ((first_address_ & mask) != network || (last_address_ & mask) != network)
    {
        throw std::invalid_argument("lease pool and gateway must belong to the configured subnet");
    }
    if (prefix_length_ <= 30U)
    {
        const auto broadcast = network | ~mask;
        if (first_address_ <= network || last_address_ >= broadcast)
        {
            throw std::invalid_argument("lease pool must exclude network and broadcast addresses");
        }
    }
}

Lease LeaseManager::acquire(const std::string& identity, const TimePoint now)
{
    if (identity.empty())
    {
        throw std::invalid_argument("lease identity must not be empty");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(reap_expired_locked(now));

    const auto existing = leases_by_identity_.find(identity);
    if (existing != leases_by_identity_.end())
    {
        throw std::runtime_error("identity already has an active virtual IP lease");
    }

    for (std::uint32_t candidate = first_address_;; ++candidate)
    {
        if (identity_by_address_.find(candidate) == identity_by_address_.end())
        {
            if (next_lease_id_ == 0U)
            {
                throw std::runtime_error("virtual IP lease generation is exhausted");
            }
            Lease lease{next_lease_id_++,
                        identity,
                        from_integer(candidate),
                        now + lease_duration_};
            leases_by_identity_.emplace(identity, lease);
            identity_by_address_.emplace(candidate, identity);
            return lease;
        }
        if (candidate == last_address_)
        {
            break;
        }
    }
    throw std::runtime_error("virtual IP lease pool is exhausted");
}

std::optional<Lease> LeaseManager::find_by_identity(const std::string& identity, const TimePoint now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(reap_expired_locked(now));
    const auto lease = leases_by_identity_.find(identity);
    if (lease == leases_by_identity_.end())
    {
        return std::nullopt;
    }
    return lease->second;
}

std::optional<Lease> LeaseManager::renew(const Lease& expected_lease,
                                        const TimePoint now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(reap_expired_locked(now));
    const auto lease = leases_by_identity_.find(expected_lease.identity);
    if (lease == leases_by_identity_.end() ||
        lease->second.lease_id != expected_lease.lease_id ||
        lease->second.address != expected_lease.address)
    {
        return std::nullopt;
    }
    lease->second.expires_at = now + lease_duration_;
    return lease->second;
}

bool LeaseManager::owns(const std::string& identity,
                        const Ipv4Address& address,
                        const TimePoint now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(reap_expired_locked(now));
    const auto lease = leases_by_identity_.find(identity);
    return lease != leases_by_identity_.end() && lease->second.address == address;
}

bool LeaseManager::release(const Lease& expected_lease)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto lease = leases_by_identity_.find(expected_lease.identity);
    if (lease == leases_by_identity_.end() ||
        lease->second.lease_id != expected_lease.lease_id ||
        lease->second.address != expected_lease.address)
    {
        return false;
    }
    identity_by_address_.erase(to_integer(lease->second.address));
    leases_by_identity_.erase(lease);
    return true;
}

std::size_t LeaseManager::reap_expired(const TimePoint now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return reap_expired_locked(now);
}

std::size_t LeaseManager::active_count(const TimePoint now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(reap_expired_locked(now));
    return leases_by_identity_.size();
}

std::size_t LeaseManager::available_count(const TimePoint now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    static_cast<void>(reap_expired_locked(now));
    const auto capacity = static_cast<std::uint64_t>(last_address_) -
                          static_cast<std::uint64_t>(first_address_) + 1U;
    return static_cast<std::size_t>(capacity - leases_by_identity_.size());
}

protocol::IpAssignment LeaseManager::to_assignment(const Lease& lease) const
{
    return protocol::IpAssignment{lease.address,
                                  gateway_address_,
                                  prefix_length_,
                                  mtu_,
                                  static_cast<std::uint32_t>(lease_duration_.count())};
}

std::uint32_t LeaseManager::to_integer(const Ipv4Address& address) noexcept
{
    return (static_cast<std::uint32_t>(address[0]) << 24U) |
           (static_cast<std::uint32_t>(address[1]) << 16U) |
           (static_cast<std::uint32_t>(address[2]) << 8U) |
           static_cast<std::uint32_t>(address[3]);
}

Ipv4Address LeaseManager::from_integer(const std::uint32_t address) noexcept
{
    return {static_cast<std::uint8_t>((address >> 24U) & 0xFFU),
            static_cast<std::uint8_t>((address >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((address >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(address & 0xFFU)};
}

std::size_t LeaseManager::reap_expired_locked(const TimePoint now)
{
    std::size_t removed = 0U;
    for (auto lease = leases_by_identity_.begin(); lease != leases_by_identity_.end();)
    {
        if (lease->second.expires_at <= now)
        {
            identity_by_address_.erase(to_integer(lease->second.address));
            lease = leases_by_identity_.erase(lease);
            ++removed;
        }
        else
        {
            ++lease;
        }
    }
    return removed;
}

} // namespace personal_vpn::core
