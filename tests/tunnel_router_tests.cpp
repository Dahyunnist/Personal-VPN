#include "personal_vpn/tunnel_router.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using namespace personal_vpn::core;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class RecordingPeer final : public TunnelPeer
{
   public:
    void send_ipv4_from_tun(std::vector<std::uint8_t> packet) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        packets_.push_back(std::move(packet));
    }

    [[nodiscard]] std::vector<std::vector<std::uint8_t>> packets() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return packets_;
    }

   private:
    mutable std::mutex mutex_;
    std::vector<std::vector<std::uint8_t>> packets_;
};

std::vector<std::uint8_t> packet_to(const Ipv4Address& destination)
{
    std::vector<std::uint8_t> packet(20U, 0U);
    packet[0] = 0x45U;
    packet[3] = 20U;
    packet[8] = 64U;
    for (std::size_t index = 0U; index < destination.size(); ++index)
    {
        packet[16U + index] = destination[index];
    }
    return packet;
}

void test_route_registration_and_delivery()
{
    TunnelRouter router;
    const auto address = parse_ipv4_address("10.8.0.2");
    auto peer = std::make_shared<RecordingPeer>();
    check(router.register_peer(address, peer), "first route registration succeeds");
    check(router.register_peer(address, peer), "same peer registration is idempotent");
    check(router.active_routes() == 1U, "idempotent registration creates one route");

    const auto packet = packet_to(address);
    check(router.route_ipv4_packet(packet), "valid packet is routed by destination lease");
    const auto delivered = peer->packets();
    check(delivered.size() == 1U && delivered.front() == packet,
          "routed peer receives an owning packet buffer");
}

void test_live_route_cannot_be_stolen_or_wrongly_removed()
{
    TunnelRouter router;
    const auto address = parse_ipv4_address("10.8.0.3");
    auto first = std::make_shared<RecordingPeer>();
    auto second = std::make_shared<RecordingPeer>();
    check(router.register_peer(address, first), "initial peer owns route");
    check(!router.register_peer(address, second), "live route cannot be replaced");
    check(!router.unregister_peer(address, second.get()),
          "stale peer cannot unregister a replacement route");
    check(router.find_peer(address).get() == first.get(), "original route remains intact");
    check(router.unregister_peer(address, first.get()), "owner can unregister its route");
    check(!router.find_peer(address), "unregistered route is absent");
}

void test_expired_route_is_reclaimed()
{
    TunnelRouter router;
    const auto address = parse_ipv4_address("10.8.0.4");
    {
        auto expired = std::make_shared<RecordingPeer>();
        check(router.register_peer(address, expired), "temporary route registers");
    }
    check(router.active_routes() == 0U, "expired weak route is reaped");
    auto replacement = std::make_shared<RecordingPeer>();
    check(router.register_peer(address, replacement), "expired route can be replaced");
}

void test_malformed_and_unroutable_packets_are_dropped()
{
    TunnelRouter router;
    auto peer = std::make_shared<RecordingPeer>();
    const auto address = parse_ipv4_address("10.8.0.5");
    check(router.register_peer(address, peer), "drop-test route registers");

    check(!router.route_ipv4_packet(std::vector<std::uint8_t>(19U, 0U)),
          "short IPv4 packet is dropped");
    auto wrong_version = packet_to(address);
    wrong_version[0] = 0x65U;
    check(!router.route_ipv4_packet(std::move(wrong_version)),
          "non-IPv4 packet is dropped");
    auto wrong_length = packet_to(address);
    wrong_length[3] = 21U;
    check(!router.route_ipv4_packet(std::move(wrong_length)),
          "inconsistent IPv4 total length is dropped");
    check(!router.route_ipv4_packet(packet_to(parse_ipv4_address("10.8.0.99"))),
          "packet without an active lease route is dropped");
    check(peer->packets().empty(), "dropped packets never reach a peer");
}

void test_concurrent_delivery()
{
    TunnelRouter router;
    const auto address = parse_ipv4_address("10.8.0.6");
    auto peer = std::make_shared<RecordingPeer>();
    check(router.register_peer(address, peer), "concurrency route registers");
    const auto packet = packet_to(address);
    constexpr std::size_t thread_count = 8U;
    constexpr std::size_t packets_per_thread = 100U;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t thread_index = 0U; thread_index < thread_count; ++thread_index)
    {
        workers.emplace_back(
            [&router, &packet]
            {
                for (std::size_t packet_index = 0U; packet_index < packets_per_thread;
                     ++packet_index)
                {
                    static_cast<void>(router.route_ipv4_packet(packet));
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    check(peer->packets().size() == thread_count * packets_per_thread,
          "concurrent routing delivers every owning packet buffer");
}

} // namespace

int main()
{
    test_route_registration_and_delivery();
    test_live_route_cannot_be_stolen_or_wrongly_removed();
    test_expired_route_is_reclaimed();
    test_malformed_and_unroutable_packets_are_dropped();
    test_concurrent_delivery();

    if (failures != 0)
    {
        std::cerr << failures << " tunnel-router test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All tunnel-router tests passed\n";
    return EXIT_SUCCESS;
}
