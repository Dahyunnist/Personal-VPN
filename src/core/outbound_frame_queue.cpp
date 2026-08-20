#include "personal_vpn/outbound_frame_queue.hpp"

#include <stdexcept>
#include <utility>

namespace personal_vpn::core
{

OutboundFrameQueue::OutboundFrameQueue(const std::size_t maximum_frames,
                                       const std::size_t maximum_bytes)
    : maximum_frames_(maximum_frames), maximum_bytes_(maximum_bytes)
{
    if (maximum_frames_ == 0U || maximum_bytes_ < protocol::kHeaderSize)
    {
        throw std::invalid_argument("outbound queue limits must allow at least one frame header");
    }
}

bool OutboundFrameQueue::try_enqueue(const protocol::Frame& frame)
{
    return try_enqueue(protocol::encode_frame(frame));
}

bool OutboundFrameQueue::try_enqueue(Buffer encoded_frame)
{
    if (encoded_frame.empty())
    {
        throw std::invalid_argument("outbound queue cannot contain an empty encoded frame");
    }
    if (queue_.size() >= maximum_frames_ || encoded_frame.size() > maximum_bytes_ - queued_bytes_)
    {
        return false;
    }

    queued_bytes_ += encoded_frame.size();
    queue_.push_back(std::make_shared<const Buffer>(std::move(encoded_frame)));
    return true;
}

const OutboundFrameQueue::BufferPtr& OutboundFrameQueue::front() const
{
    if (queue_.empty())
    {
        throw std::logic_error("outbound queue is empty");
    }
    return queue_.front();
}

void OutboundFrameQueue::pop_front()
{
    if (queue_.empty())
    {
        throw std::logic_error("outbound queue is empty");
    }
    queued_bytes_ -= queue_.front()->size();
    queue_.pop_front();
}

void OutboundFrameQueue::clear() noexcept
{
    queue_.clear();
    queued_bytes_ = 0U;
}

} // namespace personal_vpn::core
