#ifndef PERSONAL_VPN_PROTOCOL_HPP
#define PERSONAL_VPN_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace personal_vpn::protocol
{

constexpr std::uint32_t kMagic = 0x5056504EU; // "PVPN"
constexpr std::uint8_t kVersion = 1U;
constexpr std::size_t kHeaderSize = 20U;
constexpr std::size_t kMaxPayloadSize = 65'535U;

enum class MessageType : std::uint8_t
{
    ClientHello = 1U,
    IpAssign = 2U,
    DataIpv4 = 3U,
    Ping = 4U,
    Pong = 5U,
    Error = 6U,
    Close = 7U,
};

enum class ProtocolErrorCode
{
    InvalidMagic,
    UnsupportedVersion,
    UnknownMessageType,
    UnsupportedFlags,
    PayloadTooLarge,
    DecoderFailed,
};

class ProtocolError : public std::runtime_error
{
   public:
    ProtocolError(ProtocolErrorCode code, const std::string& message);

    [[nodiscard]] ProtocolErrorCode code() const noexcept { return code_; }

   private:
    ProtocolErrorCode code_;
};

struct Frame
{
    MessageType type{MessageType::Error};
    std::uint16_t flags{0U};
    std::uint64_t sequence{0U};
    std::vector<std::uint8_t> payload;

    [[nodiscard]] bool operator==(const Frame& other) const noexcept;
};

[[nodiscard]] bool is_known_message_type(std::uint8_t raw_type) noexcept;

[[nodiscard]] std::vector<std::uint8_t> encode_frame(const Frame& frame);

class FrameDecoder
{
   public:
    [[nodiscard]] std::vector<Frame> push(const std::uint8_t* data, std::size_t size);
    [[nodiscard]] std::vector<Frame> push(const std::vector<std::uint8_t>& data);

    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    void reset() noexcept;

   private:
    [[noreturn]] void fail(ProtocolErrorCode code, const std::string& message);

    std::vector<std::uint8_t> buffer_;
    std::size_t offset_{0U};
    bool failed_{false};
};

} // namespace personal_vpn::protocol

#endif
