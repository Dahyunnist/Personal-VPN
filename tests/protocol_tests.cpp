#include "personal_vpn/protocol.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using personal_vpn::protocol::Frame;
using personal_vpn::protocol::FrameDecoder;
using personal_vpn::protocol::MessageType;
using personal_vpn::protocol::ProtocolError;
using personal_vpn::protocol::ProtocolErrorCode;
using personal_vpn::protocol::encode_frame;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_protocol_error(const ProtocolErrorCode expected,
                           const std::function<void()>& operation,
                           const std::string& message)
{
    try
    {
        operation();
        check(false, message + " (no exception)");
    }
    catch (const ProtocolError& error)
    {
        check(error.code() == expected, message + " (wrong error code)");
    }
}

Frame sample_frame(const MessageType type, const std::uint64_t sequence)
{
    return Frame{type, 0U, sequence, {0x45U, 0x00U, 0x00U, 0x14U}};
}

void test_round_trip()
{
    for (std::uint8_t raw_type = 1U; raw_type <= 7U; ++raw_type)
    {
        const auto original = sample_frame(static_cast<MessageType>(raw_type),
                                           0x0102030405060708ULL + raw_type);
        FrameDecoder decoder;
        const auto decoded = decoder.push(encode_frame(original));
        check(decoded.size() == 1U, "round trip emits exactly one frame");
        check(decoded.size() == 1U && decoded.front() == original, "round trip preserves frame");
        check(decoder.buffered_bytes() == 0U, "round trip leaves no buffered bytes");
    }
}

void test_fragmented_frame()
{
    const auto original = sample_frame(MessageType::DataIpv4, 42U);
    const auto encoded = encode_frame(original);
    FrameDecoder decoder;
    std::vector<Frame> decoded;
    for (const auto byte : encoded)
    {
        const auto emitted = decoder.push(&byte, 1U);
        decoded.insert(decoded.end(), emitted.begin(), emitted.end());
    }
    check(decoded.size() == 1U && decoded.front() == original,
          "byte-at-a-time input reconstructs one frame");
}

void test_coalesced_frames()
{
    const auto first = sample_frame(MessageType::Ping, 10U);
    const auto second = sample_frame(MessageType::Pong, 11U);
    auto bytes = encode_frame(first);
    const auto second_bytes = encode_frame(second);
    bytes.insert(bytes.end(), second_bytes.begin(), second_bytes.end());

    FrameDecoder decoder;
    const auto decoded = decoder.push(bytes);
    check(decoded.size() == 2U, "coalesced input emits both frames");
    check(decoded.size() == 2U && decoded[0] == first && decoded[1] == second,
          "coalesced frames preserve order");
}

void test_partial_payload_is_buffered()
{
    const auto frame = sample_frame(MessageType::ClientHello, 1U);
    const auto encoded = encode_frame(frame);
    FrameDecoder decoder;
    const auto first = decoder.push(encoded.data(), encoded.size() - 1U);
    check(first.empty(), "partial payload does not emit a frame");
    check(decoder.buffered_bytes() == encoded.size() - 1U, "partial payload is retained");
    const auto second = decoder.push(encoded.data() + encoded.size() - 1U, 1U);
    check(second.size() == 1U && second.front() == frame, "final byte completes the frame");
}

void test_empty_input_is_a_no_op()
{
    FrameDecoder decoder;
    const std::vector<std::uint8_t> empty;
    const auto decoded = decoder.push(empty);
    check(decoded.empty(), "empty input emits no frames");
    check(decoder.buffered_bytes() == 0U, "empty input leaves no buffered bytes");
}

void test_invalid_headers_fail_closed()
{
    const auto valid = encode_frame(sample_frame(MessageType::Ping, 3U));

    auto bad_magic = valid;
    bad_magic[0] ^= 0xFFU;
    FrameDecoder magic_decoder;
    expect_protocol_error(ProtocolErrorCode::InvalidMagic,
                          [&] { static_cast<void>(magic_decoder.push(bad_magic)); },
                          "invalid magic is rejected");
    expect_protocol_error(ProtocolErrorCode::DecoderFailed,
                          [&] { static_cast<void>(magic_decoder.push(valid)); },
                          "failed decoder cannot silently resume");

    auto bad_version = valid;
    bad_version[4] = 2U;
    FrameDecoder version_decoder;
    expect_protocol_error(ProtocolErrorCode::UnsupportedVersion,
                          [&] { static_cast<void>(version_decoder.push(bad_version)); },
                          "unsupported version is rejected");

    auto bad_type = valid;
    bad_type[5] = 99U;
    FrameDecoder type_decoder;
    expect_protocol_error(ProtocolErrorCode::UnknownMessageType,
                          [&] { static_cast<void>(type_decoder.push(bad_type)); },
                          "unknown message type is rejected");

    auto bad_flags = valid;
    bad_flags[7] = 1U;
    FrameDecoder flags_decoder;
    expect_protocol_error(ProtocolErrorCode::UnsupportedFlags,
                          [&] { static_cast<void>(flags_decoder.push(bad_flags)); },
                          "unknown flags are rejected");
}

void test_oversized_payload_is_rejected_from_header()
{
    auto bytes = encode_frame(sample_frame(MessageType::DataIpv4, 4U));
    bytes.resize(personal_vpn::protocol::kHeaderSize);
    const auto oversized = static_cast<std::uint32_t>(personal_vpn::protocol::kMaxPayloadSize + 1U);
    bytes[8] = static_cast<std::uint8_t>((oversized >> 24U) & 0xFFU);
    bytes[9] = static_cast<std::uint8_t>((oversized >> 16U) & 0xFFU);
    bytes[10] = static_cast<std::uint8_t>((oversized >> 8U) & 0xFFU);
    bytes[11] = static_cast<std::uint8_t>(oversized & 0xFFU);

    FrameDecoder decoder;
    expect_protocol_error(ProtocolErrorCode::PayloadTooLarge,
                          [&] { static_cast<void>(decoder.push(bytes)); },
                          "oversized declared payload is rejected before allocation");
}

} // namespace

int main()
{
    test_round_trip();
    test_fragmented_frame();
    test_coalesced_frames();
    test_partial_payload_is_buffered();
    test_empty_input_is_a_no_op();
    test_invalid_headers_fail_closed();
    test_oversized_payload_is_rejected_from_header();

    if (failures != 0)
    {
        std::cerr << failures << " protocol test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All protocol tests passed\n";
    return EXIT_SUCCESS;
}
