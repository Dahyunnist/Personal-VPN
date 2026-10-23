#include "personal_vpn/broker_runtime_registry.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace personal_vpn;

int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

class FakeRuntime final : public client::ClientRuntimeControl
{
   public:
    explicit FakeRuntime(EventHandler event_handler,
                         std::atomic<int>& stopped,
                         std::atomic<int>& waited,
                         const bool fail_stop = false)
        : event_handler_(std::move(event_handler)),
          stopped_(stopped),
          waited_(waited),
          fail_stop_(fail_stop)
    {
    }

    void start() override { publish(client::ClientRuntimeState::Resolving); }

    void stop() override
    {
        ++stopped_;
        publish(client::ClientRuntimeState::Stopped);
        if (fail_stop_)
        {
            throw std::runtime_error("simulated stop failure");
        }
    }

    void wait() override { ++waited_; }

    [[nodiscard]] client::ClientRuntimeState state() const noexcept override
    {
        return state_.load();
    }

    [[nodiscard]] std::string failure_message() const override { return {}; }

    void publish(const client::ClientRuntimeState state)
    {
        state_.store(state);
        if (event_handler_)
        {
            event_handler_({state, {}});
        }
    }

   private:
    EventHandler event_handler_;
    std::atomic<int>& stopped_;
    std::atomic<int>& waited_;
    std::atomic<client::ClientRuntimeState> state_{client::ClientRuntimeState::Stopped};
    bool fail_stop_;
};

void dispatch_lifecycle()
{
    std::atomic<int> stopped{0};
    std::atomic<int> waited{0};
    std::vector<FakeRuntime*> created;
    broker::BrokerRuntimeRegistry registry(
        [&stopped, &waited, &created](const std::string& profile,
                                      client::ClientRuntimeControl::EventHandler handler)
        {
            if (profile != "summer_profile")
            {
                throw std::runtime_error("unexpected profile ID");
            }
            auto runtime = std::make_unique<FakeRuntime>(std::move(handler), stopped, waited);
            created.push_back(runtime.get());
            return runtime;
        });

    const auto disconnected = registry.dispatch("logon-sid-A",
                                               {broker::RequestType::GetStatus, 1U, {}});
    check(disconnected.type == broker::ResponseType::Status && !disconnected.connected &&
              disconnected.message == "disconnected",
          "status reports disconnected when caller has no runtime");

    const auto connected = registry.dispatch(
        "logon-sid-A", {broker::RequestType::Connect, 2U, "summer_profile"});
    check(connected.type == broker::ResponseType::Accepted && !connected.connected &&
              connected.message == "connecting" && created.size() == 1U,
          "connect creates one runtime and returns startup status");
    static_cast<void>(broker::encode_response(connected));

    const auto duplicate = registry.dispatch(
        "logon-sid-A", {broker::RequestType::Connect, 3U, "summer_profile"});
    check(duplicate.type == broker::ResponseType::Rejected && created.size() == 1U,
          "one caller cannot create duplicate active runtimes");

    created.front()->publish(client::ClientRuntimeState::Connected);
    const auto status = registry.dispatch(
        "logon-sid-A", {broker::RequestType::GetStatus, 4U, {}});
    check(status.type == broker::ResponseType::Status && status.connected &&
              status.message == "connected",
          "status reflects asynchronous runtime events");

    const auto stopped_response = registry.dispatch(
        "logon-sid-A", {broker::RequestType::Disconnect, 5U, {}});
    check(stopped_response.type == broker::ResponseType::Accepted && !stopped_response.connected &&
              stopped == 1 && waited == 1,
          "disconnect stops and joins the caller runtime");
    const auto idempotent_stop = registry.dispatch(
        "logon-sid-A", {broker::RequestType::Disconnect, 6U, {}});
    check(idempotent_stop.type == broker::ResponseType::Accepted && stopped == 1,
          "disconnect is idempotent for a caller without a runtime");
}

void shutdown_stops_all_callers()
{
    std::atomic<int> stopped{0};
    std::atomic<int> waited{0};
    int created = 0;
    broker::BrokerRuntimeRegistry registry(
        [&stopped, &waited, &created](const std::string&,
                                      client::ClientRuntimeControl::EventHandler handler)
        {
            const bool fail_stop = created++ == 0;
            return std::make_unique<FakeRuntime>(std::move(handler),
                                                 stopped,
                                                 waited,
                                                 fail_stop);
        });
    static_cast<void>(registry.dispatch("sid-A", {broker::RequestType::Connect, 1U, "a"}));
    static_cast<void>(registry.dispatch("sid-B", {broker::RequestType::Connect, 2U, "b"}));
    registry.shutdown();
    check(stopped == 2 && waited == 2,
          "service shutdown attempts join even when another stop reports failure");
    const auto status = registry.dispatch("sid-A", {broker::RequestType::GetStatus, 3U, {}});
    check(!status.connected && status.message == "disconnected",
          "shutdown clears all registry entries");
}

void factory_failures_are_sanitized()
{
    broker::BrokerRuntimeRegistry registry(
        [](const std::string&, client::ClientRuntimeControl::EventHandler)
            -> std::unique_ptr<client::ClientRuntimeControl>
        { throw std::runtime_error("secret path and credential details"); });
    const auto response = registry.dispatch(
        "user-sid", {broker::RequestType::Connect, 1U, "profile"});
    check(response.type == broker::ResponseType::Rejected &&
              response.message == "profile or runtime creation failed" &&
              response.message.find("secret") == std::string::npos,
          "runtime construction errors are not exposed to IPC callers");
}

} // namespace

int main()
{
    dispatch_lifecycle();
    shutdown_stops_all_callers();
    factory_failures_are_sanitized();
    if (failures != 0)
    {
        return EXIT_FAILURE;
    }
    std::cout << "Broker runtime registry tests passed\n";
    return EXIT_SUCCESS;
}
