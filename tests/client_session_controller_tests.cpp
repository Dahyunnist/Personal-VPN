#include "personal_vpn/client_session_controller.hpp"

#include <cstdlib>
#include <functional>
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

IpAssignment valid_assignment()
{
    return IpAssignment{{10U, 8U, 0U, 2U},
                        {10U, 8U, 0U, 1U},
                        24U,
                        1'280U,
                        3'600U};
}

std::vector<std::uint8_t> ipv4_packet(const std::array<std::uint8_t, 4U>& source,
                                      const std::array<std::uint8_t, 4U>& destination)
{
    std::vector<std::uint8_t> packet(20U, 0U);
    packet[0] = 0x45U;
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

void establish(ClientSessionController& controller)
{
    const auto hello = controller.start();
    check(hello.type == MessageType::ClientHello && hello.sequence == 1U &&
              decode_client_hello(hello.payload) == ClientHello{1'400U, kCapabilityIpv4},
          "client starts with a versioned IPv4 CLIENT_HELLO");
    const auto assignment = valid_assignment();
    const auto result = controller.handle(
        Frame{MessageType::IpAssign, 0U, 1U, encode_ip_assignment(assignment)});
    check(!result.close_transport && result.assignment == assignment &&
              controller.state() == ClientSessionState::Established,
          "valid server assignment establishes the client session");
}

ErrorMessage result_error(const ClientSessionResult& result)
{
    if (result.outbound_frames.size() != 1U ||
        result.outbound_frames.front().type != MessageType::Error)
    {
        return ErrorMessage{0U, "missing client error frame"};
    }
    return decode_error_message(result.outbound_frames.front().payload);
}

void test_server_authoritative_assignment_and_bidirectional_data()
{
    ClientSessionController controller;
    establish(controller);
    const auto assignment = valid_assignment();
    const auto uplink = ipv4_packet(assignment.client_address, {192U, 0U, 2U, 10U});
    const auto uplink_frame = controller.make_data_to_server(uplink);
    check(uplink_frame.has_value() && uplink_frame->type == MessageType::DataIpv4 &&
              uplink_frame->sequence == 2U && uplink_frame->payload == uplink,
          "local packet with assigned source becomes a sequenced DATA_IPV4 frame");

    const auto downlink = ipv4_packet({192U, 0U, 2U, 10U}, assignment.client_address);
    const auto result =
        controller.handle(Frame{MessageType::DataIpv4, 0U, 2U, downlink});
    check(result.packets_to_tun.size() == 1U && result.packets_to_tun.front() == downlink,
          "server packet for the assigned destination reaches Wintun boundary");
}

void test_assignment_policy_is_enforced()
{
    ClientSessionController wrong_subnet;
    static_cast<void>(wrong_subnet.start());
    auto assignment = valid_assignment();
    assignment.gateway_address = {10U, 9U, 0U, 1U};
    const auto subnet_result = wrong_subnet.handle(
        Frame{MessageType::IpAssign, 0U, 1U, encode_ip_assignment(assignment)});
    check(subnet_result.close_transport &&
              result_error(subnet_result).code ==
                  static_cast<std::uint16_t>(ClientSessionErrorCode::InvalidAssignment),
          "assignment outside the gateway subnet fails closed");

    ClientSessionController excessive_mtu(1'280U);
    static_cast<void>(excessive_mtu.start());
    assignment = valid_assignment();
    assignment.mtu = 1'400U;
    const auto mtu_result = excessive_mtu.handle(
        Frame{MessageType::IpAssign, 0U, 1U, encode_ip_assignment(assignment)});
    check(mtu_result.close_transport &&
              result_error(mtu_result).code ==
                  static_cast<std::uint16_t>(ClientSessionErrorCode::InvalidAssignment),
          "server cannot raise the client-requested MTU");

    ClientSessionController network_address;
    static_cast<void>(network_address.start());
    assignment = valid_assignment();
    assignment.client_address = {10U, 8U, 0U, 0U};
    const auto network_result = network_address.handle(
        Frame{MessageType::IpAssign, 0U, 1U, encode_ip_assignment(assignment)});
    check(network_result.close_transport &&
              result_error(network_result).code ==
                  static_cast<std::uint16_t>(ClientSessionErrorCode::InvalidAssignment),
          "network address cannot be assigned to the client");
}

void test_address_spoofing_is_blocked_in_both_directions()
{
    ClientSessionController controller;
    establish(controller);
    const auto bad_uplink = ipv4_packet({10U, 8U, 0U, 99U}, {192U, 0U, 2U, 1U});
    check(!controller.make_data_to_server(bad_uplink).has_value(),
          "local packet with an unassigned source is dropped");

    const auto bad_downlink = ipv4_packet({192U, 0U, 2U, 1U}, {10U, 8U, 0U, 99U});
    const auto result =
        controller.handle(Frame{MessageType::DataIpv4, 0U, 2U, bad_downlink});
    check(result.close_transport && result.packets_to_tun.empty() &&
              result_error(result).code ==
                  static_cast<std::uint16_t>(ClientSessionErrorCode::AddressMismatch),
          "server packet for another lease fails closed before Wintun");
}

void test_sequence_liveness_and_remote_close()
{
    ClientSessionController bad_sequence;
    static_cast<void>(bad_sequence.start());
    const auto sequence_result = bad_sequence.handle(
        Frame{MessageType::IpAssign, 0U, 2U, encode_ip_assignment(valid_assignment())});
    check(sequence_result.close_transport &&
              result_error(sequence_result).code ==
                  static_cast<std::uint16_t>(ClientSessionErrorCode::InvalidSequence),
          "server sequence gap fails closed");

    ClientSessionController liveness;
    establish(liveness);
    const LivenessProbe probe{0xABCDEFU};
    const auto pong = liveness.handle(
        Frame{MessageType::Ping, 0U, 2U, encode_liveness_probe(probe)});
    check(pong.outbound_frames.size() == 1U &&
              pong.outbound_frames.front().type == MessageType::Pong &&
              pong.outbound_frames.front().sequence == 2U &&
              decode_liveness_probe(pong.outbound_frames.front().payload) == probe,
          "server PING receives a monotonic PONG");

    const auto client_ping = liveness.make_ping(77U);
    check(client_ping.has_value() && client_ping->type == MessageType::Ping &&
              client_ping->sequence == 3U && !liveness.make_ping(88U).has_value(),
          "client allows only one outstanding liveness probe");
    const auto matching_pong = liveness.handle(
        Frame{MessageType::Pong, 0U, 3U, encode_liveness_probe(LivenessProbe{77U})});
    check(!matching_pong.close_transport && liveness.make_ping(88U).has_value(),
          "matching PONG clears the outstanding client probe");

    const auto closed = liveness.handle(
        Frame{MessageType::Close, 0U, 4U, encode_close_message(CloseMessage{0U})});
    check(closed.close_transport && liveness.state() == ClientSessionState::Closed,
          "server CLOSE terminates the client state machine");

    ClientSessionController wrong_pong;
    establish(wrong_pong);
    static_cast<void>(wrong_pong.make_ping(1U));
    const auto wrong_pong_result = wrong_pong.handle(
        Frame{MessageType::Pong, 0U, 2U, encode_liveness_probe(LivenessProbe{2U})});
    check(wrong_pong_result.close_transport,
          "mismatched PONG nonce fails the client session closed");
}

void test_remote_error_is_preserved_for_ui()
{
    ClientSessionController controller;
    establish(controller);
    const ErrorMessage remote{4U, "lease unavailable"};
    const auto result = controller.handle(
        Frame{MessageType::Error, 0U, 2U, encode_error_message(remote)});
    check(result.close_transport && result.remote_error == remote &&
              controller.state() == ClientSessionState::Closed,
          "server ERROR is retained as structured UI diagnostic state");
}

} // namespace

int main()
{
    test_server_authoritative_assignment_and_bidirectional_data();
    test_assignment_policy_is_enforced();
    test_address_spoofing_is_blocked_in_both_directions();
    test_sequence_liveness_and_remote_close();
    test_remote_error_is_preserved_for_ui();

    if (failures != 0)
    {
        std::cerr << failures << " client-session test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All client-session tests passed\n";
    return EXIT_SUCCESS;
}
