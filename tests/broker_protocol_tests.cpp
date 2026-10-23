#include "personal_vpn/broker_protocol.hpp"
#if defined(_WIN32) && defined(PERSONAL_VPN_HAS_BROKER_RUNTIME)
#include "personal_vpn/windows_broker_pipe.hpp"
#include "personal_vpn/windows_broker_profile.hpp"
#include "personal_vpn/windows_broker_profile_loader.hpp"
#include "personal_vpn/windows_broker_pipe_session.hpp"
#include "personal_vpn/windows_broker_client_runtime.hpp"
#include "personal_vpn/broker_runtime_registry.hpp"
#include <sddl.h>
#endif

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32) && defined(PERSONAL_VPN_HAS_BROKER_RUNTIME)
#include <atomic>
#include <thread>
#include <chrono>
#endif

namespace
{

int failures = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename Operation>
void check_rejected(Operation operation, const std::string& message)
{
    try
    {
        operation();
        check(false, message);
    }
    catch (const std::invalid_argument&)
    {
    }
}

void overwrite_u16(std::vector<std::uint8_t>& frame,
                   const std::size_t offset,
                   const std::uint16_t value)
{
    frame[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    frame[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void overwrite_u64(std::vector<std::uint8_t>& frame,
                   const std::size_t offset,
                   const std::uint64_t value)
{
    for (unsigned int index = 0U; index < 8U; ++index)
    {
        frame[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void round_trip()
{
    using personal_vpn::broker::Request;
    using personal_vpn::broker::RequestType;
    using personal_vpn::broker::decode_request;
    using personal_vpn::broker::encode_request;

    const std::vector<Request> requests{{RequestType::Connect, 1U, "summer-vpn_1"},
                                         {RequestType::Disconnect, 2U, {}},
                                         {RequestType::GetStatus, 3U, {}}};
    for (const auto& request : requests)
    {
        const auto decoded = decode_request(encode_request(request));
        check(decoded.type == request.type && decoded.request_id == request.request_id &&
                  decoded.profile_id == request.profile_id,
              "supported broker request round-trips");
    }
    const auto maximum_profile = std::string(personal_vpn::broker::maximum_profile_id_size, 'p');
    const auto maximum_frame = encode_request({RequestType::Connect, 4U, maximum_profile});
    check(maximum_frame.size() <= personal_vpn::broker::maximum_frame_size &&
              decode_request(maximum_frame).profile_id == maximum_profile,
          "maximum-size valid profile fits the bounded frame");
}

void response_round_trip_and_limits()
{
    using namespace personal_vpn::broker;
    for (const auto type : {ResponseType::Accepted, ResponseType::Status, ResponseType::Rejected})
    {
        const Response expected{type, 77U, type == ResponseType::Status, "状态已更新"};
        const auto actual = decode_response(encode_response(expected));
        check(actual.type == expected.type && actual.request_id == expected.request_id &&
                  actual.connected == expected.connected && actual.message == expected.message,
              "broker responses round-trip with correlation ID and bounded status");
    }
    check_rejected([]
                   {
                       static_cast<void>(encode_response(
                           {ResponseType::Status, 0U, false, {}}));
                   },
                   "response encoder rejects zero correlation IDs");
    check_rejected([]
                   {
                       static_cast<void>(encode_response(
                           {ResponseType::Rejected, 1U, false,
                            std::string(maximum_response_message_size + 1U, 'x')}));
                   },
                   "response encoder rejects oversized diagnostics");
}

void reject_invalid_inputs()
{
    using personal_vpn::broker::Request;
    using personal_vpn::broker::RequestType;
    using personal_vpn::broker::decode_request;
    using personal_vpn::broker::encode_request;

    check_rejected([] { static_cast<void>(encode_request({RequestType::GetStatus, 0U, {}})); },
                   "encoder rejects a zero request ID");
    check_rejected([] { static_cast<void>(encode_request({RequestType::Connect, 1U, ""})); },
                   "encoder rejects an empty profile ID");
    check_rejected([]
                   { static_cast<void>(encode_request({RequestType::Connect, 1U, "../secret"})); },
                   "encoder rejects path-like profile IDs");
    check_rejected([]
                   {
                       static_cast<void>(encode_request(
                           {RequestType::Connect, 1U, std::string(65U, 'a')}));
                   },
                   "encoder rejects an oversized profile ID");
    check_rejected([]
                   { static_cast<void>(encode_request({RequestType::GetStatus, 1U, "extra"})); },
                   "encoder rejects unexpected fields");

    auto valid = encode_request({RequestType::GetStatus, 7U, {}});
    auto bad_magic = valid;
    bad_magic[4U] ^= 0xFFU;
    check_rejected([&] { static_cast<void>(decode_request(bad_magic)); },
                   "decoder rejects invalid magic");
    auto bad_version = valid;
    overwrite_u16(bad_version, 8U, 2U);
    check_rejected([&] { static_cast<void>(decode_request(bad_version)); },
                   "decoder rejects unsupported protocol versions");
    auto bad_type = valid;
    overwrite_u16(bad_type, 10U, 99U);
    check_rejected([&] { static_cast<void>(decode_request(bad_type)); },
                   "decoder rejects unknown request types");
    auto zero_id = valid;
    overwrite_u64(zero_id, 12U, 0U);
    check_rejected([&] { static_cast<void>(decode_request(zero_id)); },
                   "decoder rejects a zero request ID");
    auto bad_length = valid;
    bad_length[0U] += 1U;
    check_rejected([&] { static_cast<void>(decode_request(bad_length)); },
                   "decoder rejects a mismatched frame length");
    auto trailing_data = valid;
    trailing_data.push_back(0U);
    check_rejected([&] { static_cast<void>(decode_request(trailing_data)); },
                   "decoder rejects trailing bytes");
    check_rejected([] { static_cast<void>(decode_request({})); },
                   "decoder rejects a truncated frame");
    check_rejected([]
                   {
                       static_cast<void>(decode_request(
                           std::vector<std::uint8_t>(
                               personal_vpn::broker::maximum_frame_size + 1U)));
                   },
                   "decoder rejects a frame over the maximum size");

    auto connect = encode_request({RequestType::Connect, 8U, "profile"});
    overwrite_u16(connect, 20U, 65U);
    check_rejected([&] { static_cast<void>(decode_request(connect)); },
                   "decoder rejects an oversized profile length");
    connect = encode_request({RequestType::Connect, 9U, "profile"});
    connect.back() = static_cast<std::uint8_t>('/');
    check_rejected([&] { static_cast<void>(decode_request(connect)); },
                   "decoder rejects path characters in profile IDs");
}

void reject_connection_replays()
{
    personal_vpn::broker::RequestReplayGuard guard;
    check(!guard.accept(0U), "replay guard rejects zero request IDs");
    check(guard.accept(12U), "replay guard accepts first request ID");
    check(!guard.accept(12U), "replay guard rejects duplicate request IDs");
    check(guard.accept(11U), "replay guard allows distinct out-of-order request IDs");

    personal_vpn::broker::RequestDecoder decoder;
    const auto frame = personal_vpn::broker::encode_request(
        {personal_vpn::broker::RequestType::GetStatus, 55U, {}});
    static_cast<void>(decoder.decode(frame));
    check_rejected([&] { static_cast<void>(decoder.decode(frame)); },
                   "connection decoder rejects duplicate request before dispatch");
}

#if defined(_WIN32) && defined(PERSONAL_VPN_HAS_BROKER_RUNTIME)
std::wstring current_logon_sid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        throw std::runtime_error("OpenProcessToken failed");
    }
    std::unique_ptr<void, decltype(&CloseHandle)> token_owner(token, CloseHandle);
    DWORD size = 0U;
    static_cast<void>(GetTokenInformation(token, TokenGroups, nullptr, 0U, &size));
    std::vector<std::uint8_t> buffer(size);
    if (size == 0U || !GetTokenInformation(token, TokenGroups, buffer.data(), size, &size))
    {
        throw std::runtime_error("GetTokenInformation(TokenGroups) failed");
    }
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
    for (DWORD index = 0U; index < groups->GroupCount; ++index)
    {
        const auto& group = groups->Groups[index];
        if ((group.Attributes & SE_GROUP_LOGON_ID) != SE_GROUP_LOGON_ID)
        {
            continue;
        }
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(group.Sid, &sid))
        {
            throw std::runtime_error("ConvertSidToStringSidW failed");
        }
        const std::wstring result(sid);
        static_cast<void>(LocalFree(sid));
        return result;
    }
    throw std::runtime_error("current token has no logon SID");
}

void named_pipe_authentication_and_framing()
{
    using namespace personal_vpn::broker;
    DWORD session_id = 0U;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    {
        throw std::runtime_error("ProcessIdToSessionId failed");
    }
    const auto sid = current_logon_sid();
    const auto server = create_broker_pipe_server(sid);
    std::unique_ptr<void, decltype(&CloseHandle)> server_owner(server, CloseHandle);
    HANDLE completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (completed == nullptr)
    {
        throw std::runtime_error("CreateEventW failed");
    }
    std::unique_ptr<void, decltype(&CloseHandle)> completed_owner(completed, CloseHandle);
    std::atomic<bool> client_ok{false};
    std::thread client([completed, &client_ok]
    {
        const auto pipe = connect_broker_pipe_client(1'000U);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }
        std::unique_ptr<void, decltype(&CloseHandle)> pipe_owner(pipe, CloseHandle);
        if (!write_broker_request(pipe, {RequestType::GetStatus, 101U, {}}))
        {
            return;
        }
        try
        {
            const auto response = read_broker_response(pipe, 101U);
            client_ok.store(response.type == ResponseType::Status && response.connected &&
                            response.message == "connected");
        }
        catch (...)
        {
        }
        static_cast<void>(WaitForSingleObject(completed, 5'000U));
    });
    RequestDecoder decoder;
    std::exception_ptr server_error;
    try
    {
        const bool accepted = accept_broker_pipe_client(server);
        if (accepted)
        {
            const auto request = read_authenticated_broker_request(server, sid, session_id, decoder);
            check(request.type == RequestType::GetStatus && request.request_id == 101U,
                  "named pipe authenticates current logon and decodes one request");
            check(write_broker_response(server,
                                        {ResponseType::Status, 101U, true, "connected"}),
                  "named pipe writes a bounded response");
        }
        else
        {
            check(false, "named pipe accepts a local client");
        }
    }
    catch (...)
    {
        server_error = std::current_exception();
    }
    static_cast<void>(SetEvent(completed));
    client.join();
    check(client_ok.load(), "named pipe returns a correlated response to the client");
    if (server_error)
    {
        std::rethrow_exception(server_error);
    }
}

class PipeTestRuntime final : public personal_vpn::client::ClientRuntimeControl
{
   public:
    PipeTestRuntime(EventHandler handler,
                    std::atomic<int>& stopped,
                    std::atomic<int>& waited,
                    const bool connected_on_start = false)
        : handler_(std::move(handler)),
          stopped_(stopped),
          waited_(waited),
          connected_on_start_(connected_on_start)
    {
    }

    void start() override
    {
        state_ = connected_on_start_ ? personal_vpn::client::ClientRuntimeState::Connected
                                     : personal_vpn::client::ClientRuntimeState::Resolving;
        handler_({state_, {}});
    }

    void stop() override
    {
        ++stopped_;
        state_ = personal_vpn::client::ClientRuntimeState::Stopped;
        handler_({state_, {}});
    }

    void wait() override { ++waited_; }

    [[nodiscard]] personal_vpn::client::ClientRuntimeState state() const noexcept override
    {
        return state_.load();
    }

    [[nodiscard]] std::string failure_message() const override { return {}; }

   private:
    EventHandler handler_;
    std::atomic<int>& stopped_;
    std::atomic<int>& waited_;
    bool connected_on_start_{false};
    std::atomic<personal_vpn::client::ClientRuntimeState> state_{
        personal_vpn::client::ClientRuntimeState::Stopped};
};

void broker_pipe_session_dispatches_and_cleans_up()
{
    using namespace personal_vpn::broker;
    DWORD session_id = 0U;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    {
        throw std::runtime_error("ProcessIdToSessionId failed for dispatcher test");
    }
    const auto sid = current_logon_sid();
    const auto server = create_broker_pipe_server(sid);
    std::unique_ptr<void, decltype(&CloseHandle)> server_owner(server, CloseHandle);

    std::atomic<int> stopped{0};
    std::atomic<int> waited{0};
    BrokerRuntimeRegistry registry(
        [&stopped, &waited](const std::string& profile_id,
                            personal_vpn::client::ClientRuntimeControl::EventHandler handler)
        {
            static_cast<void>(resolve_impersonated_profile_path(profile_id));
            return std::make_unique<PipeTestRuntime>(std::move(handler), stopped, waited);
        });
    std::atomic<bool> client_ok{false};
    std::thread client([&client_ok]
    {
        const auto pipe = connect_broker_pipe_client(1'000U);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }
        std::unique_ptr<void, decltype(&CloseHandle)> pipe_owner(pipe, CloseHandle);
        if (!write_broker_request(pipe,
                                  {RequestType::Connect, 202U, "profile_dispatch_test"}))
        {
            return;
        }
        try
        {
            const auto response = read_broker_response(pipe, 202U);
            client_ok.store(response.type == ResponseType::Accepted && !response.connected &&
                            response.message == "connecting");
        }
        catch (...)
        {
        }
    });

    if (!accept_broker_pipe_client(server))
    {
        client.join();
        throw std::runtime_error("broker dispatcher pipe accept failed");
    }
    serve_broker_pipe_session(server, sid, session_id, registry);
    client.join();
    check(client_ok.load(), "pipe session dispatches Connect and returns correlated response");
    check(stopped == 1 && waited == 1,
          "pipe disconnect stops and joins the runtime created under caller impersonation");
}

void broker_client_runtime_controls_a_persistent_pipe_session()
{
    using namespace personal_vpn::broker;
    DWORD session_id = 0U;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session_id))
    {
        throw std::runtime_error("ProcessIdToSessionId failed for broker client test");
    }
    const auto sid = current_logon_sid();
    const auto server = create_broker_pipe_server(sid);
    std::unique_ptr<void, decltype(&CloseHandle)> server_owner(server, CloseHandle);

    std::atomic<int> stopped{0};
    std::atomic<int> waited{0};
    BrokerRuntimeRegistry registry(
        [&stopped, &waited](const std::string&,
                            personal_vpn::client::ClientRuntimeControl::EventHandler handler)
        {
            return std::make_unique<PipeTestRuntime>(
                std::move(handler), stopped, waited, true);
        });
    std::thread server_thread([&]
    {
        if (accept_broker_pipe_client(server))
        {
            serve_broker_pipe_session(server, sid, session_id, registry);
        }
    });

    std::atomic<bool> saw_status{false};
    BrokerClientRuntimeControl runtime(
        "broker_client_test",
        [&saw_status](const personal_vpn::client::ClientRuntimeEvent& event)
        {
            if (event.state == personal_vpn::client::ClientRuntimeState::Connected &&
                event.message == "VPN 已连接")
            {
                saw_status = true;
            }
        });
    runtime.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!saw_status.load() && runtime.state() != personal_vpn::client::ClientRuntimeState::Failed &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(saw_status.load(), "broker client receives correlated Status on persistent pipe");
    runtime.stop();
    runtime.wait();
    server_thread.join();
    check(runtime.state() == personal_vpn::client::ClientRuntimeState::Stopped,
          "broker client reaches Stopped after Disconnect");
    check(stopped == 1 && waited == 1,
          "broker client Disconnect stops and joins the service runtime");
}

void profile_paths_are_bound_to_the_impersonated_user()
{
    using namespace personal_vpn::broker;
    bool missing_identity_rejected = false;
    try
    {
        static_cast<void>(resolve_impersonated_profile_path("profile_1"));
    }
    catch (const std::system_error&)
    {
        missing_identity_rejected = true;
    }
    check(missing_identity_rejected,
          "profile path resolution requires an authenticated impersonation token");

    bool traversal_id_rejected = false;
    try
    {
        static_cast<void>(resolve_impersonated_profile_path("../outside"));
    }
    catch (const std::invalid_argument&)
    {
        traversal_id_rejected = true;
    }
    check(traversal_id_rejected, "profile path resolver rejects traversal IDs");

    HANDLE source = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source))
    {
        throw std::runtime_error("OpenProcessToken for profile test failed");
    }
    std::unique_ptr<void, decltype(&CloseHandle)> source_owner(source, CloseHandle);
    HANDLE impersonation = nullptr;
    if (!DuplicateTokenEx(source,
                          TOKEN_QUERY | TOKEN_IMPERSONATE,
                          nullptr,
                          SecurityImpersonation,
                          TokenImpersonation,
                          &impersonation))
    {
        throw std::runtime_error("DuplicateTokenEx for profile test failed");
    }
    std::unique_ptr<void, decltype(&CloseHandle)> impersonation_owner(impersonation, CloseHandle);
    if (!SetThreadToken(nullptr, impersonation))
    {
        throw std::runtime_error("SetThreadToken for profile test failed");
    }
    std::filesystem::path resolved;
    try
    {
        resolved = resolve_impersonated_profile_path("profile_1");
        bool missing_profile_rejected = false;
        try
        {
            static_cast<void>(load_impersonated_profile(
                "profile_missing_for_broker_test",
                personal_vpn::client::ClientNetworkPolicy{{"10.20.0.0/16"}, false}));
        }
        catch (const std::invalid_argument&)
        {
            missing_profile_rejected = true;
        }
        check(missing_profile_rejected,
              "broker profile loading uses only the derived caller path and trusted policy");
    }
    catch (...)
    {
        static_cast<void>(RevertToSelf());
        throw;
    }
    if (!RevertToSelf())
    {
        throw std::runtime_error("RevertToSelf for profile test failed");
    }
    check(resolved.filename() == L"profile_1.json" &&
              resolved.parent_path().filename() == L"profiles" &&
              resolved.parent_path().parent_path().filename() == L"PersonalVPN",
          "profile path is derived under the caller's fixed PersonalVPN profile root");
}
#endif

} // namespace

int main()
{
    round_trip();
    response_round_trip_and_limits();
    reject_invalid_inputs();
    reject_connection_replays();
#if defined(_WIN32) && defined(PERSONAL_VPN_HAS_BROKER_RUNTIME)
    try
    {
        named_pipe_authentication_and_framing();
        broker_pipe_session_dispatches_and_cleans_up();
        broker_client_runtime_controls_a_persistent_pipe_session();
        profile_paths_are_bound_to_the_impersonated_user();
    }
    catch (const std::exception& error)
    {
        check(false, std::string("named pipe integration failed: ") + error.what());
    }
#endif
    if (failures != 0)
    {
        std::cerr << failures << " broker protocol test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All broker protocol tests passed\n";
    return EXIT_SUCCESS;
}
