#include "personal_vpn/broker_protocol.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Operation>
void check_rejected(Operation operation, const std::string& message)
{
    try
    {
        operation();
        check(false, message);
    }
    catch (const std::invalid_argument&)
    {
    }
}

void overwrite_u16(std::vector<std::uint8_t>& frame,
                   const std::size_t offset,
                   const std::uint16_t value)
{
    frame[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    frame[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void overwrite_u64(std::vector<std::uint8_t>& frame,
                   const std::size_t offset,
                   const std::uint64_t value)
{
    for (unsigned int index = 0U; index < 8U; ++index)
    {
        frame[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void round_trip()
{
    using personal_vpn::broker::Request;
    using personal_vpn::broker::RequestType;
    using personal_vpn::broker::decode_request;
    using personal_vpn::broker::encode_request;

    const std::vector<Request> requests{{RequestType::Connect, 1U, "summer-vpn_1"},
                                         {RequestType::Disconnect, 2U, {}},
                                         {RequestType::GetStatus, 3U, {}}};
    for (const auto& request : requests)
    {
        const auto decoded = decode_request(encode_request(request));
        check(decoded.type == request.type && decoded.request_id == request.request_id &&
                  decoded.profile_id == request.profile_id,
              "supported broker request round-trips");
    }
    const auto maximum_profile = std::string(personal_vpn::broker::maximum_profile_id_size, 'p');
    const auto maximum_frame = encode_request({RequestType::Connect, 4U, maximum_profile});
    check(maximum_frame.size() <= personal_vpn::broker::maximum_frame_size &&
              decode_request(maximum_frame).profile_id == maximum_profile,
          "maximum-size valid profile fits the bounded frame");
}

void reject_invalid_inputs()
{
    using personal_vpn::broker::Request;
    using personal_vpn::broker::RequestType;
    using personal_vpn::broker::decode_request;
    using personal_vpn::broker::encode_request;

    check_rejected([] { static_cast<void>(encode_request({RequestType::GetStatus, 0U, {}})); },
                   "encoder rejects a zero request ID");
    check_rejected([] { static_cast<void>(encode_request({RequestType::Connect, 1U, ""})); },
                   "encoder rejects an empty profile ID");
    check_rejected([]
                   { static_cast<void>(encode_request({RequestType::Connect, 1U, "../secret"})); },
                   "encoder rejects path-like profile IDs");
    check_rejected([]
                   {
                       static_cast<void>(encode_request(
                           {RequestType::Connect, 1U, std::string(65U, 'a')}));
                   },
                   "encoder rejects an oversized profile ID");
    check_rejected([]
                   { static_cast<void>(encode_request({RequestType::GetStatus, 1U, "extra"})); },
                   "encoder rejects unexpected fields");

    auto valid = encode_request({RequestType::GetStatus, 7U, {}});
    auto bad_magic = valid;
    bad_magic[4U] ^= 0xFFU;
    check_rejected([&] { static_cast<void>(decode_request(bad_magic)); },
                   "decoder rejects invalid magic");
    auto bad_version = valid;
    overwrite_u16(bad_version, 8U, 2U);
    check_rejected([&] { static_cast<void>(decode_request(bad_version)); },
                   "decoder rejects unsupported protocol versions");
    auto bad_type = valid;
    overwrite_u16(bad_type, 10U, 99U);
    check_rejected([&] { static_cast<void>(decode_request(bad_type)); },
                   "decoder rejects unknown request types");
    auto zero_id = valid;
    overwrite_u64(zero_id, 12U, 0U);
    check_rejected([&] { static_cast<void>(decode_request(zero_id)); },
                   "decoder rejects a zero request ID");
    auto bad_length = valid;
    bad_length[0U] += 1U;
    check_rejected([&] { static_cast<void>(decode_request(bad_length)); },
                   "decoder rejects a mismatched frame length");
    auto trailing_data = valid;
    trailing_data.push_back(0U);
    check_rejected([&] { static_cast<void>(decode_request(trailing_data)); },
                   "decoder rejects trailing bytes");
    check_rejected([] { static_cast<void>(decode_request({})); },
                   "decoder rejects a truncated frame");
    check_rejected([]
                   {
                       static_cast<void>(decode_request(
                           std::vector<std::uint8_t>(
                               personal_vpn::broker::maximum_frame_size + 1U)));
                   },
                   "decoder rejects a frame over the maximum size");

    auto connect = encode_request({RequestType::Connect, 8U, "profile"});
    overwrite_u16(connect, 20U, 65U);
    check_rejected([&] { static_cast<void>(decode_request(connect)); },
                   "decoder rejects an oversized profile length");
    connect = encode_request({RequestType::Connect, 9U, "profile"});
    connect.back() = static_cast<std::uint8_t>('/');
    check_rejected([&] { static_cast<void>(decode_request(connect)); },
                   "decoder rejects path characters in profile IDs");
}

} // namespace

int main()
{
    round_trip();
    reject_invalid_inputs();
    if (failures != 0)
    {
        std::cerr << failures << " broker protocol test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All broker protocol tests passed\n";
    return EXIT_SUCCESS;
}
