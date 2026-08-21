#include "personal_vpn/control_messages.hpp"
#include "personal_vpn/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

void decode_control_frame(const personal_vpn::protocol::Frame& frame)
{
    using personal_vpn::protocol::MessageType;
    switch (frame.type)
    {
        case MessageType::ClientHello:
            static_cast<void>(personal_vpn::protocol::decode_client_hello(frame.payload));
            break;
        case MessageType::IpAssign:
            static_cast<void>(personal_vpn::protocol::decode_ip_assignment(frame.payload));
            break;
        case MessageType::Ping:
        case MessageType::Pong:
            static_cast<void>(personal_vpn::protocol::decode_liveness_probe(frame.payload));
            break;
        case MessageType::Error:
            static_cast<void>(personal_vpn::protocol::decode_error_message(frame.payload));
            break;
        case MessageType::Close:
            static_cast<void>(personal_vpn::protocol::decode_close_message(frame.payload));
            break;
        case MessageType::DataIpv4:
            break;
    }
}

void feed_fragmented(personal_vpn::protocol::FrameDecoder& decoder,
                     const std::uint8_t* data,
                     const std::size_t size,
                     const std::size_t stride)
{
    std::size_t offset = 0U;
    while (offset < size)
    {
        const auto remaining = size - offset;
        const auto chunk = remaining < stride ? remaining : stride;
        const auto frames = decoder.push(data + offset, chunk);
        for (const auto& frame : frames)
        {
            decode_control_frame(frame);
        }
        offset += chunk;
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    if (data == nullptr || size == 0U)
    {
        return 0;
    }
    try
    {
        // Exercise arbitrary wire input, including malformed and fragmented headers.
        personal_vpn::protocol::FrameDecoder decoder;
        const auto stride = size % 31U + 1U;
        feed_fragmented(decoder, data, size, stride);

        // Also turn arbitrary input into a valid frame. This keeps the mutator focused on
        // control-message decoders instead of requiring it to rediscover the full header.
        if (size <= personal_vpn::protocol::kMaxPayloadSize + 1U)
        {
            const auto type_value = static_cast<std::uint8_t>(data[0] % 7U + 1U);
            const auto type = static_cast<personal_vpn::protocol::MessageType>(type_value);
            const std::vector<std::uint8_t> payload(data + 1U, data + size);
            const personal_vpn::protocol::Frame frame{type, 0U, 1U, payload};
            const auto encoded = personal_vpn::protocol::encode_frame(frame);
            personal_vpn::protocol::FrameDecoder valid_decoder;
            feed_fragmented(valid_decoder, encoded.data(), encoded.size(), stride);
        }
    }
    catch (...)
    {
    }
    return 0;
}
