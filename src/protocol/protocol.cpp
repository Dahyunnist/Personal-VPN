#include "personal_vpn/protocol.hpp"

#include <algorithm>
#include <limits>

namespace personal_vpn::protocol
{
namespace
{

void append_u16(std::vector<std::uint8_t>& output, const std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64(std::vector<std::uint8_t>& output, const std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        output.push_back(static_cast<std::uint8_t>((value >> static_cast<unsigned int>(shift)) & 0xFFU));
    }
}

std::uint16_t read_u16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) |
                                      static_cast<std::uint16_t>(data[1]));
}

std::uint32_t read_u32(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64(const std::uint8_t* data)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index)
    {
        value = (value << 8U) | static_cast<std::uint64_t>(data[index]);
    }
    return value;
}

} // namespace

ProtocolError::ProtocolError(const ProtocolErrorCode code, const std::string& message)
    : std::runtime_error(message), code_(code)
{
}

bool Frame::operator==(const Frame& other) const noexcept
{
    return type == other.type && flags == other.flags && sequence == other.sequence &&
           payload == other.payload;
}

bool is_known_message_type(const std::uint8_t raw_type) noexcept
{
    return raw_type >= static_cast<std::uint8_t>(MessageType::ClientHello) &&
           raw_type <= static_cast<std::uint8_t>(MessageType::Close);
}

std::vector<std::uint8_t> encode_frame(const Frame& frame)
{
    if (!is_known_message_type(static_cast<std::uint8_t>(frame.type)))
    {
        throw ProtocolError(ProtocolErrorCode::UnknownMessageType, "unknown message type");
    }
    if (frame.flags != 0U)
    {
        throw ProtocolError(ProtocolErrorCode::UnsupportedFlags, "protocol v1 requires zero flags");
    }
    if (frame.payload.size() > kMaxPayloadSize ||
        frame.payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        throw ProtocolError(ProtocolErrorCode::PayloadTooLarge, "frame payload exceeds the protocol limit");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(kHeaderSize + frame.payload.size());
    append_u32(encoded, kMagic);
    encoded.push_back(kVersion);
    encoded.push_back(static_cast<std::uint8_t>(frame.type));
    append_u16(encoded, frame.flags);
    append_u32(encoded, static_cast<std::uint32_t>(frame.payload.size()));
    append_u64(encoded, frame.sequence);
    encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
    return encoded;
}

std::vector<Frame> FrameDecoder::push(const std::uint8_t* data, const std::size_t size)
{
    if (failed_)
    {
        throw ProtocolError(ProtocolErrorCode::DecoderFailed,
                            "decoder is in a failed state; reset it before reuse");
    }
    if (size > 0U && data == nullptr)
    {
        throw std::invalid_argument("data must not be null when size is non-zero");
    }

    if (offset_ > 0U)
    {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ = 0U;
    }
    if (size > 0U)
    {
        buffer_.insert(buffer_.end(), data, data + size);
    }

    std::vector<Frame> frames;
    while (buffer_.size() - offset_ >= kHeaderSize)
    {
        const auto* header = buffer_.data() + offset_;
        if (read_u32(header) != kMagic)
        {
            fail(ProtocolErrorCode::InvalidMagic, "invalid tunnel frame magic");
        }
        if (header[4] != kVersion)
        {
            fail(ProtocolErrorCode::UnsupportedVersion, "unsupported tunnel protocol version");
        }
        if (!is_known_message_type(header[5]))
        {
            fail(ProtocolErrorCode::UnknownMessageType, "unknown tunnel message type");
        }
        const auto flags = read_u16(header + 6U);
        if (flags != 0U)
        {
            fail(ProtocolErrorCode::UnsupportedFlags, "unsupported tunnel frame flags");
        }
        const auto payload_size = static_cast<std::size_t>(read_u32(header + 8U));
        if (payload_size > kMaxPayloadSize)
        {
            fail(ProtocolErrorCode::PayloadTooLarge, "declared payload exceeds the protocol limit");
        }
        if (buffer_.size() - offset_ < kHeaderSize + payload_size)
        {
            break;
        }

        Frame frame;
        frame.type = static_cast<MessageType>(header[5]);
        frame.flags = flags;
        frame.sequence = read_u64(header + 12U);
        const auto payload_begin = buffer_.begin() + static_cast<std::ptrdiff_t>(offset_ + kHeaderSize);
        frame.payload.assign(payload_begin,
                             payload_begin + static_cast<std::ptrdiff_t>(payload_size));
        frames.push_back(std::move(frame));
        offset_ += kHeaderSize + payload_size;
    }

    if (offset_ == buffer_.size())
    {
        buffer_.clear();
        offset_ = 0U;
    }
    return frames;
}

std::vector<Frame> FrameDecoder::push(const std::vector<std::uint8_t>& data)
{
    return push(data.data(), data.size());
}

std::size_t FrameDecoder::buffered_bytes() const noexcept
{
    return buffer_.size() - offset_;
}

void FrameDecoder::reset() noexcept
{
    buffer_.clear();
    offset_ = 0U;
    failed_ = false;
}

[[noreturn]] void FrameDecoder::fail(const ProtocolErrorCode code, const std::string& message)
{
    failed_ = true;
    throw ProtocolError(code, message);
}

} // namespace personal_vpn::protocol
