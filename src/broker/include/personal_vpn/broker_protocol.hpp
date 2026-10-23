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

enum class ResponseType : std::uint16_t
{
    Accepted = 1,
    Status = 2,
    Rejected = 3,
};

struct Response
{
    ResponseType type{ResponseType::Status};
    std::uint64_t request_id{0U};
    bool connected{false};
    std::string message;
};

inline constexpr std::size_t maximum_frame_size = 128U;
inline constexpr std::size_t maximum_response_size = 512U;
inline constexpr std::size_t maximum_profile_id_size = 64U;
inline constexpr std::size_t maximum_response_message_size = 480U;

[[nodiscard]] std::vector<std::uint8_t> encode_request(const Request& request);
[[nodiscard]] Request decode_request(const std::vector<std::uint8_t>& frame);
[[nodiscard]] std::vector<std::uint8_t> encode_response(const Response& response);
[[nodiscard]] Response decode_response(const std::vector<std::uint8_t>& frame);

class RequestReplayGuard final
{
   public:
    [[nodiscard]] bool accept(std::uint64_t request_id);

   private:
    static constexpr std::size_t maximum_requests_per_connection = 256U;
    std::unordered_set<std::uint64_t> seen_;
};

class RequestDecoder final
{
   public:
    [[nodiscard]] Request decode(const std::vector<std::uint8_t>& frame);

   private:
    RequestReplayGuard replay_guard_;
};

} // namespace personal_vpn::broker

#endif
