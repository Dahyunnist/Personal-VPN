#include "personal_vpn/protocol.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

std::size_t parse_iterations(const int argc, char* argv[])
{
    if (argc == 1)
    {
        return 200'000U;
    }
    if (argc != 2)
    {
        throw std::invalid_argument("usage: protocol-benchmark [iterations]");
    }
    const auto parsed = std::stoull(argv[1]);
    if (parsed == 0U || parsed > 100'000'000U)
    {
        throw std::invalid_argument("iterations must be between 1 and 100000000");
    }
    return static_cast<std::size_t>(parsed);
}

double megabytes_per_second(const std::size_t total_bytes,
                            const std::chrono::steady_clock::duration duration)
{
    const auto seconds = std::chrono::duration<double>(duration).count();
    return static_cast<double>(total_bytes) / (1024.0 * 1024.0) / seconds;
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        using namespace personal_vpn::protocol;
        const auto iterations = parse_iterations(argc, argv);
        std::vector<std::uint8_t> packet(1'400U, 0xA5U);
        packet[0] = 0x45U;
        const Frame frame{MessageType::DataIpv4, 0U, 42U, packet};

        std::vector<std::uint8_t> encoded;
        std::uint64_t checksum = 0U;
        const auto encode_start = std::chrono::steady_clock::now();
        for (std::size_t index = 0U; index < iterations; ++index)
        {
            encoded = encode_frame(frame);
            checksum += encoded[index % encoded.size()];
        }
        const auto encode_duration = std::chrono::steady_clock::now() - encode_start;

        FrameDecoder decoder;
        const auto decode_start = std::chrono::steady_clock::now();
        for (std::size_t index = 0U; index < iterations; ++index)
        {
            const auto frames = decoder.push(encoded);
            checksum += frames.front().sequence + frames.front().payload[index % packet.size()];
        }
        const auto decode_duration = std::chrono::steady_clock::now() - decode_start;
        const auto total_bytes = encoded.size() * iterations;
        std::cout << "{\"benchmark\":\"protocol_frame_1400\",\"iterations\":"
                  << iterations << ",\"encoded_bytes\":" << encoded.size()
                  << ",\"encode_mib_per_second\":"
                  << megabytes_per_second(total_bytes, encode_duration)
                  << ",\"decode_mib_per_second\":"
                  << megabytes_per_second(total_bytes, decode_duration)
                  << ",\"checksum\":" << checksum << "}\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "protocol-benchmark: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
