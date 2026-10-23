#ifndef PERSONAL_VPN_BROKER_PROTOCOL_HPP
#define PERSONAL_VPN_BROKER_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace personal_vpn::broker
{

enum class RequestType : std::uint16_t
{
    Connect = 1,
    Disconnect = 2,
    GetStatus = 3,
};

struct Request
{
    RequestType type{RequestType::GetStatus};
    std::uint64_t request_id{0U};
    std::string profile_id;
};

inline constexpr std::size_t maximum_frame_size = 128U;
inline constexpr std::size_t maximum_profile_id_size = 64U;

[[nodiscard]] std::vector<std::uint8_t> encode_request(const Request& request);
[[nodiscard]] Request decode_request(const std::vector<std::uint8_t>& frame);

class RequestReplayGuard final
{
   public:
    [[nodiscard]] bool accept(std::uint64_t request_id);

   private:
    static constexpr std::size_t maximum_requests_per_connection = 256U;
    std::unordered_set<std::uint64_t> seen_;
};

} // namespace personal_vpn::broker

#endif
