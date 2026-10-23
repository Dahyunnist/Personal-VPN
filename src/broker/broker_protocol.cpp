#include "personal_vpn/broker_protocol.hpp"

#include <stdexcept>

namespace personal_vpn::broker
{
namespace
{

constexpr std::uint8_t magic[] = {'P', 'V', 'B', 'R'};
constexpr std::uint16_t protocol_version = 1U;
constexpr std::size_t frame_header_size = 20U;

void append_u16(std::vector<std::uint8_t>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_u64(std::vector<std::uint8_t>& output, const std::uint64_t value)
{
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
    {
        output.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& input, const std::size_t offset)
{
    return static_cast<std::uint16_t>(input[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(input[offset + 1U]) << 8U);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& input, const std::size_t offset)
{
    std::uint32_t value = 0U;
    for (unsigned int index = 0U; index < 4U; ++index)
    {
        value |= static_cast<std::uint32_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::vector<std::uint8_t>& input, const std::size_t offset)
{
    std::uint64_t value = 0U;
    for (unsigned int index = 0U; index < 8U; ++index)
    {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

bool valid_profile_id(const std::string& profile_id) noexcept
{
    if (profile_id.empty() || profile_id.size() > maximum_profile_id_size)
    {
        return false;
    }
    for (const unsigned char character : profile_id)
    {
        const bool ascii_letter = (character >= 'a' && character <= 'z') ||
                                  (character >= 'A' && character <= 'Z');
        const bool digit = character >= '0' && character <= '9';
        if (!ascii_letter && !digit && character != '-' && character != '_')
        {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<std::uint8_t> encode_request(const Request& request)
{
    if (request.request_id == 0U)
    {
        throw std::invalid_argument("broker request ID must be nonzero");
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(maximum_frame_size);
    frame.push_back(0U);
    frame.push_back(0U);
    frame.push_back(0U);
    frame.push_back(0U);
    frame.insert(frame.end(), std::begin(magic), std::end(magic));
    append_u16(frame, protocol_version);
    append_u16(frame, static_cast<std::uint16_t>(request.type));
    append_u64(frame, request.request_id);

    switch (request.type)
    {
        case RequestType::Connect:
            if (!valid_profile_id(request.profile_id))
            {
                throw std::invalid_argument("broker profile ID is invalid");
            }
            append_u16(frame, static_cast<std::uint16_t>(request.profile_id.size()));
            frame.insert(frame.end(), request.profile_id.begin(), request.profile_id.end());
            break;
        case RequestType::Disconnect:
        case RequestType::GetStatus:
            if (!request.profile_id.empty())
            {
                throw std::invalid_argument("broker request has an unexpected profile ID");
            }
            break;
        default:
            throw std::invalid_argument("broker request type is unknown");
    }

    const auto body_size = frame.size() - 4U;
    if (frame.size() > maximum_frame_size)
    {
        throw std::invalid_argument("broker request exceeds the maximum frame size");
    }
    const auto body_length = static_cast<std::uint32_t>(body_size);
    for (unsigned int index = 0U; index < 4U; ++index)
    {
        frame[index] = static_cast<std::uint8_t>((body_length >> (index * 8U)) & 0xFFU);
    }
    return frame;
}

Request decode_request(const std::vector<std::uint8_t>& frame)
{
    if (frame.size() < frame_header_size || frame.size() > maximum_frame_size)
    {
        throw std::invalid_argument("broker frame size is invalid");
    }
    if (read_u32(frame, 0U) != frame.size() - 4U)
    {
        throw std::invalid_argument("broker frame length prefix is invalid");
    }
    for (std::size_t index = 0U; index < sizeof(magic); ++index)
    {
        if (frame[4U + index] != magic[index])
        {
            throw std::invalid_argument("broker frame magic is invalid");
        }
    }
    if (read_u16(frame, 8U) != protocol_version)
    {
        throw std::invalid_argument("broker protocol version is unsupported");
    }

    Request request;
    request.type = static_cast<RequestType>(read_u16(frame, 10U));
    request.request_id = read_u64(frame, 12U);
    if (request.request_id == 0U)
    {
        throw std::invalid_argument("broker request ID must be nonzero");
    }

    switch (request.type)
    {
        case RequestType::Connect:
        {
            if (frame.size() < frame_header_size + 2U)
            {
                throw std::invalid_argument("broker connect request is truncated");
            }
            const auto profile_size = static_cast<std::size_t>(read_u16(frame, frame_header_size));
            if (profile_size > maximum_profile_id_size ||
                frame.size() != frame_header_size + 2U + profile_size)
            {
                throw std::invalid_argument("broker profile ID length is invalid");
            }
            request.profile_id.assign(frame.begin() +
                                          static_cast<std::ptrdiff_t>(frame_header_size + 2U),
                                      frame.end());
            if (!valid_profile_id(request.profile_id))
            {
                throw std::invalid_argument("broker profile ID is invalid");
            }
            break;
        }
        case RequestType::Disconnect:
        case RequestType::GetStatus:
            if (frame.size() != frame_header_size)
            {
                throw std::invalid_argument("broker request has an unexpected payload");
            }
            break;
        default:
            throw std::invalid_argument("broker request type is unknown");
    }
    return request;
}

bool RequestReplayGuard::accept(const std::uint64_t request_id)
{
    if (request_id == 0U || seen_.size() >= maximum_requests_per_connection)
    {
        return false;
    }
    return seen_.insert(request_id).second;
}

Request RequestDecoder::decode(const std::vector<std::uint8_t>& frame)
{
    auto request = decode_request(frame);
    if (!replay_guard_.accept(request.request_id))
    {
        throw std::invalid_argument("broker request ID was replayed or connection limit was reached");
    }
    return request;
}

} // namespace personal_vpn::broker
