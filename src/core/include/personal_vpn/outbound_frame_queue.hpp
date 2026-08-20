#ifndef PERSONAL_VPN_OUTBOUND_FRAME_QUEUE_HPP
#define PERSONAL_VPN_OUTBOUND_FRAME_QUEUE_HPP

#include "personal_vpn/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace personal_vpn::core
{

class OutboundFrameQueue
{
   public:
    using Buffer = std::vector<std::uint8_t>;
    using BufferPtr = std::shared_ptr<const Buffer>;

    OutboundFrameQueue(std::size_t maximum_frames, std::size_t maximum_bytes);

    [[nodiscard]] bool try_enqueue(const protocol::Frame& frame);
    [[nodiscard]] bool try_enqueue(Buffer encoded_frame);

    [[nodiscard]] const BufferPtr& front() const;
    void pop_front();
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return queue_.size(); }
    [[nodiscard]] std::size_t queued_bytes() const noexcept { return queued_bytes_; }
    [[nodiscard]] std::size_t maximum_frames() const noexcept { return maximum_frames_; }
    [[nodiscard]] std::size_t maximum_bytes() const noexcept { return maximum_bytes_; }

   private:
    const std::size_t maximum_frames_;
    const std::size_t maximum_bytes_;
    std::size_t queued_bytes_{0U};
    std::deque<BufferPtr> queue_;
};

} // namespace personal_vpn::core

#endif
