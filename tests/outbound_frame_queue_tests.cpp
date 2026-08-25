#include "personal_vpn/outbound_frame_queue.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace personal_vpn::core;
using namespace personal_vpn::protocol;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

Frame frame(const std::uint64_t sequence, const std::size_t payload_size)
{
    return Frame{MessageType::DataIpv4,
                 0U,
                 sequence,
                 std::vector<std::uint8_t>(payload_size, static_cast<std::uint8_t>(sequence))};
}

Frame decode(const OutboundFrameQueue::BufferPtr& bytes)
{
    FrameDecoder decoder;
    const auto frames = decoder.push(*bytes);
    if (frames.size() != 1U)
    {
        throw std::runtime_error("queued buffer did not contain exactly one frame");
    }
    return frames.front();
}

void test_fifo_and_byte_accounting()
{
    OutboundFrameQueue queue(3U, 1'024U);
    const auto first = frame(1U, 10U);
    const auto second = frame(2U, 20U);
    check(queue.try_enqueue(first), "first frame is accepted");
    check(queue.try_enqueue(second), "second frame is accepted");
    check(queue.size() == 2U, "queue counts frames");
    check(queue.queued_bytes() == 2U * kHeaderSize + 30U, "queue counts encoded bytes");
    check(decode(queue.front()) == first, "queue preserves FIFO order");
    queue.pop_front();
    check(decode(queue.front()) == second, "pop exposes next frame");
    check(queue.queued_bytes() == kHeaderSize + 20U, "pop decrements byte count");
    queue.pop_front();
    check(queue.empty() && queue.queued_bytes() == 0U, "drained queue has zero state");
}

void test_queue_owns_encoded_data()
{
    OutboundFrameQueue queue(2U, 1'024U);
    auto original = frame(7U, 8U);
    const auto expected = original;
    check(queue.try_enqueue(original), "frame is enqueued");
    original.payload.assign(8U, 0xFFU);
    original.sequence = 999U;
    check(decode(queue.front()) == expected, "queued bytes do not alias caller frame storage");
}

void test_frame_and_byte_backpressure()
{
    OutboundFrameQueue frame_limited(2U, 10'000U);
    check(frame_limited.try_enqueue(frame(1U, 1U)), "frame limit queue accepts first");
    check(frame_limited.try_enqueue(frame(2U, 1U)), "frame limit queue accepts second");
    check(!frame_limited.try_enqueue(frame(3U, 1U)), "frame limit rejects overflow");
    check(frame_limited.size() == 2U, "rejected frame does not mutate queue");

    OutboundFrameQueue byte_limited(10U, 2U * kHeaderSize + 10U);
    check(byte_limited.try_enqueue(frame(1U, 10U)), "byte limit queue accepts fitting frame");
    check(!byte_limited.try_enqueue(frame(2U, 1U)), "byte limit rejects overflow");
    check(byte_limited.queued_bytes() == kHeaderSize + 10U,
          "rejected bytes do not mutate accounting");
}

void test_clear_and_empty_errors()
{
    OutboundFrameQueue queue(2U, 1'024U);
    check(queue.try_enqueue(frame(1U, 10U)), "queue accepts frame before clear");
    queue.clear();
    check(queue.empty() && queue.queued_bytes() == 0U, "clear releases all queued buffers");
    try
    {
        static_cast<void>(queue.front());
        check(false, "front on empty queue throws");
    }
    catch (const std::logic_error&)
    {
    }
}

} // namespace

int main()
{
    test_fifo_and_byte_accounting();
    test_queue_owns_encoded_data();
    test_frame_and_byte_backpressure();
    test_clear_and_empty_errors();

    if (failures != 0)
    {
        std::cerr << failures << " outbound-queue test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All outbound-queue tests passed\n";
    return EXIT_SUCCESS;
}
