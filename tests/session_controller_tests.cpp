#include "personal_vpn/session_controller.hpp"

#include "personal_vpn/control_messages.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace personal_vpn::core;
using namespace personal_vpn::protocol;
using namespace std::chrono_literals;

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

LeaseManager make_manager(const std::string& last = "10.8.0.10")
{
    return LeaseManager(parse_ipv4_address("10.8.0.2"),
                        parse_ipv4_address(last),
                        parse_ipv4_address("10.8.0.1"),
                        24U,
                        1'400U,
                        60s);
}

Frame hello_frame(const std::uint64_t sequence = 1U)
{
    return Frame{MessageType::ClientHello,
                 0U,
                 sequence,
                 encode_client_hello(ClientHello{1'280U, kCapabilityIpv4})};
}

std::vector<std::uint8_t> ipv4_packet(const Ipv4Address source,
                                      const Ipv4Address destination = {192U, 0U, 2U, 1U})
{
    std::vector<std::uint8_t> packet(20U, 0U);
    packet[0] = 0x45U;
    packet[2] = 0U;
    packet[3] = 20U;
    packet[8] = 64U;
    packet[9] = 1U;
    for (std::size_t index = 0U; index < 4U; ++index)
    {
        packet[12U + index] = source[index];
        packet[16U + index] = destination[index];
    }
    return packet;
}

ErrorMessage result_error(const SessionResult& result)
{
    if (result.outbound_frames.size() != 1U ||
        result.outbound_frames.front().type != MessageType::Error)
    {
        return ErrorMessage{0U, "missing error frame"};
    }
    return decode_error_message(result.outbound_frames.front().payload);
}

void establish(SessionController& controller, const LeaseManager::TimePoint now)
{
    const auto result = controller.handle(hello_frame(), now);
    check(!result.close_transport, "valid hello keeps transport open");
    check(result.outbound_frames.size() == 1U &&
              result.outbound_frames.front().type == MessageType::IpAssign,
          "valid hello produces IP_ASSIGN");
    check(controller.state() == SessionState::Established, "valid hello establishes session");
}

void test_hello_assigns_server_lease()
{
    auto manager = make_manager();
    SessionController controller(manager, "cert:alice");
    const auto result = controller.handle(hello_frame(), LeaseManager::TimePoint{});
    const auto assignment = decode_ip_assignment(result.outbound_frames.front().payload);
    check(assignment.client_address == parse_ipv4_address("10.8.0.2"),
          "server chooses first lease address");
    check(assignment.gateway_address == parse_ipv4_address("10.8.0.1"),
          "server chooses gateway");
    check(assignment.mtu == 1'280U, "server negotiates the lower requested MTU");
    check(result.outbound_frames.front().sequence == 1U, "outbound sequence starts at one");
}

void test_valid_packet_is_delivered()
{
    auto manager = make_manager();
    SessionController controller(manager, "cert:alice");
    const auto now = LeaseManager::TimePoint{};
    establish(controller, now);
    const auto packet = ipv4_packet(parse_ipv4_address("10.8.0.2"));
    const auto result = controller.handle(Frame{MessageType::DataIpv4, 0U, 2U, packet}, now);
    check(result.packets_to_tun.size() == 1U && result.packets_to_tun.front() == packet,
          "valid leased packet is delivered to TUN");
    check(result.outbound_frames.empty() && !result.close_transport,
          "valid packet produces no control response");
}

void test_source_spoofing_is_rejected()
{
    auto manager = make_manager();
    SessionController controller(manager, "cert:alice");
    const auto now = LeaseManager::TimePoint{};
    establish(controller, now);
    const auto packet = ipv4_packet(parse_ipv4_address("10.8.0.99"));
    const auto result = controller.handle(Frame{MessageType::DataIpv4, 0U, 2U, packet}, now);
    check(result.close_transport, "source spoofing closes transport");
    check(result.packets_to_tun.empty(), "source spoofing never reaches TUN");
    check(result_error(result).code ==
              static_cast<std::uint16_t>(SessionErrorCode::SourceAddressMismatch),
          "source spoofing reports the policy error");
}

void test_message_order_and_sequence_are_enforced()
{
    auto manager = make_manager();
    const auto now = LeaseManager::TimePoint{};
    SessionController no_hello(manager, "cert:no-hello");
    const auto early_data = no_hello.handle(
        Frame{MessageType::DataIpv4,
              0U,
              1U,
              ipv4_packet(parse_ipv4_address("10.8.0.2"))},
        now);
    check(early_data.close_transport, "data before hello closes transport");
    check(result_error(early_data).code ==
              static_cast<std::uint16_t>(SessionErrorCode::UnexpectedMessage),
          "data before hello reports message ordering error");

    SessionController bad_sequence(manager, "cert:bad-sequence");
    const auto sequence_result = bad_sequence.handle(hello_frame(2U), now);
    check(sequence_result.close_transport, "unexpected initial sequence closes transport");
    check(result_error(sequence_result).code ==
              static_cast<std::uint16_t>(SessionErrorCode::InvalidSequence),
          "unexpected sequence reports sequence error");
}

void test_ping_pong_and_close()
{
    auto manager = make_manager();
    SessionController controller(manager, "cert:alice");
    const auto now = LeaseManager::TimePoint{};
    establish(controller, now);
    const LivenessProbe probe{123456U};
    const auto pong = controller.handle(
        Frame{MessageType::Ping, 0U, 2U, encode_liveness_probe(probe)}, now);
    check(pong.outbound_frames.size() == 1U &&
              pong.outbound_frames.front().type == MessageType::Pong &&
              decode_liveness_probe(pong.outbound_frames.front().payload) == probe,
          "PING returns PONG with the same nonce");
    check(pong.outbound_frames.front().sequence == 2U, "outbound sequence is monotonic");

    const auto closed = controller.handle(
        Frame{MessageType::Close, 0U, 3U, encode_close_message(CloseMessage{0U})}, now);
    check(closed.close_transport && controller.state() == SessionState::Closed,
          "CLOSE releases and closes session");
    check(manager.active_count(now) == 0U, "closed session releases its lease");
}

void test_malformed_ipv4_is_rejected()
{
    auto manager = make_manager();
    SessionController controller(manager, "cert:alice");
    const auto now = LeaseManager::TimePoint{};
    establish(controller, now);
    auto packet = ipv4_packet(parse_ipv4_address("10.8.0.2"));
    packet[0] = 0x65U;
    const auto result = controller.handle(Frame{MessageType::DataIpv4, 0U, 2U, packet}, now);
    check(result.close_transport && result.packets_to_tun.empty(),
          "non-IPv4 packet is rejected before TUN");
    check(result_error(result).code ==
              static_cast<std::uint16_t>(SessionErrorCode::InvalidIpv4Packet),
          "malformed IPv4 reports packet error");
}

void test_pool_exhaustion_is_protocol_error()
{
    auto manager = make_manager("10.8.0.2");
    const auto now = LeaseManager::TimePoint{};
    SessionController alice(manager, "cert:alice");
    establish(alice, now);
    SessionController bob(manager, "cert:bob");
    const auto result = bob.handle(hello_frame(), now);
    check(result.close_transport, "lease exhaustion closes new session");
    check(result_error(result).code ==
              static_cast<std::uint16_t>(SessionErrorCode::LeaseUnavailable),
          "lease exhaustion returns stable protocol error");
}

void test_activity_renews_but_cannot_revive_expired_lease()
{
    auto manager = make_manager();
    const auto start = LeaseManager::TimePoint{};
    SessionController active(manager, "cert:active");
    establish(active, start);
    const LivenessProbe probe{77U};
    const auto pong = active.handle(
        Frame{MessageType::Ping, 0U, 2U, encode_liveness_probe(probe)}, start + 30s);
    check(!pong.close_transport, "activity before expiry renews lease");
    check(manager.owns("cert:active", parse_ipv4_address("10.8.0.2"), start + 70s),
          "renewed lease remains active beyond original deadline");

    SessionController expired(manager, "cert:expired");
    establish(expired, start);
    const auto packet = ipv4_packet(expired.lease()->address);
    const auto result = expired.handle(
        Frame{MessageType::DataIpv4, 0U, 2U, packet}, start + 61s);
    check(result.close_transport && result.packets_to_tun.empty(),
          "activity cannot revive an already expired lease");
    check(result_error(result).code ==
              static_cast<std::uint16_t>(SessionErrorCode::LeaseUnavailable),
          "expired session reports lease error");
}

void test_server_to_client_packet_targets_lease()
{
    auto manager = make_manager();
    const auto now = LeaseManager::TimePoint{};
    SessionController controller(manager, "cert:alice");
    establish(controller, now);

    const auto valid = ipv4_packet(parse_ipv4_address("192.0.2.1"),
                                   parse_ipv4_address("10.8.0.2"));
    const auto frame = controller.make_data_to_client(valid, now);
    check(frame.has_value() && frame->type == MessageType::DataIpv4 && frame->payload == valid,
          "TUN packet addressed to lease becomes DATA_IPV4");
    check(frame.has_value() && frame->sequence == 2U,
          "server data shares the monotonic outbound sequence");

    const auto unrelated = ipv4_packet(parse_ipv4_address("192.0.2.1"),
                                       parse_ipv4_address("10.8.0.99"));
    check(!controller.make_data_to_client(unrelated, now).has_value(),
          "TUN packet for another address is not sent to session");
}

} // namespace

int main()
{
    test_hello_assigns_server_lease();
    test_valid_packet_is_delivered();
    test_source_spoofing_is_rejected();
    test_message_order_and_sequence_are_enforced();
    test_ping_pong_and_close();
    test_malformed_ipv4_is_rejected();
    test_pool_exhaustion_is_protocol_error();
    test_activity_renews_but_cannot_revive_expired_lease();
    test_server_to_client_packet_targets_lease();

    if (failures != 0)
    {
        std::cerr << failures << " session-controller test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All session-controller tests passed\n";
    return EXIT_SUCCESS;
}
