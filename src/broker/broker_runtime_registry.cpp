#include "personal_vpn/broker_runtime_registry.hpp"

#include <atomic>
#include <stdexcept>
#include <utility>
#include <vector>

namespace personal_vpn::broker
{
namespace
{

std::string status_message(const client::ClientRuntimeState state)
{
    switch (state)
    {
        case client::ClientRuntimeState::Starting:
        case client::ClientRuntimeState::Resolving:
        case client::ClientRuntimeState::Connecting:
        case client::ClientRuntimeState::ConfiguringNetwork:
            return "connecting";
        case client::ClientRuntimeState::Connected:
            return "connected";
        case client::ClientRuntimeState::Reconnecting:
            return "reconnecting";
        case client::ClientRuntimeState::Stopping:
            return "disconnecting";
        case client::ClientRuntimeState::Failed:
            return "failed";
        case client::ClientRuntimeState::Stopped:
            return "disconnected";
    }
    return "unknown";
}

Response rejected(const std::uint64_t request_id, const char* message)
{
    return {ResponseType::Rejected, request_id, false, message};
}

bool stop_and_wait(client::ClientRuntimeControl& runtime) noexcept
{
    bool succeeded = true;
    try
    {
        runtime.stop();
    }
    catch (...)
    {
        succeeded = false;
    }
    try
    {
        runtime.wait();
    }
    catch (...)
    {
        succeeded = false;
    }
    return succeeded;
}

} // namespace

struct BrokerRuntimeRegistry::Session
{
    std::atomic<client::ClientRuntimeState> state{client::ClientRuntimeState::Starting};
    std::unique_ptr<client::ClientRuntimeControl> runtime;
};

BrokerRuntimeRegistry::BrokerRuntimeRegistry(RuntimeFactory runtime_factory)
    : runtime_factory_(std::move(runtime_factory))
{
    if (!runtime_factory_)
    {
        throw std::invalid_argument("broker runtime factory is required");
    }
}

BrokerRuntimeRegistry::~BrokerRuntimeRegistry()
{
    shutdown();
}

Response BrokerRuntimeRegistry::dispatch(const std::string& peer_user_sid,
                                         const Request& request)
{
    if (request.request_id == 0U)
    {
        throw std::invalid_argument("broker dispatch requires a nonzero request ID");
    }
    if (peer_user_sid.empty())
    {
        return rejected(request.request_id, "caller identity is unavailable");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto session = sessions_.find(peer_user_sid);
    switch (request.type)
    {
        case RequestType::Connect:
        {
            if (!is_valid_profile_id(request.profile_id))
            {
                return rejected(request.request_id, "profile ID is invalid");
            }
            if (session != sessions_.end())
            {
                return rejected(request.request_id, "caller already has an active session");
            }
            auto state = std::make_shared<Session>();
            const std::weak_ptr<Session> weak_state(state);
            try
            {
                state->runtime = runtime_factory_(
                    request.profile_id,
                    [weak_state](const client::ClientRuntimeEvent& event)
                    {
                        if (const auto locked = weak_state.lock())
                        {
                            locked->state.store(event.state);
                        }
                    });
            }
            catch (...)
            {
                return rejected(request.request_id, "profile or runtime creation failed");
            }
            if (!state->runtime)
            {
                return rejected(request.request_id, "runtime could not be created");
            }
            sessions_.emplace(peer_user_sid, state);
            try
            {
                state->runtime->start();
            }
            catch (...)
            {
                sessions_.erase(peer_user_sid);
                static_cast<void>(stop_and_wait(*state->runtime));
                return rejected(request.request_id, "runtime failed to start");
            }
            return {ResponseType::Accepted,
                    request.request_id,
                    state->state.load() == client::ClientRuntimeState::Connected,
                    status_message(state->state.load())};
        }
        case RequestType::Disconnect:
        {
            if (session == sessions_.end())
            {
                return {ResponseType::Accepted, request.request_id, false, "disconnected"};
            }
            auto state = session->second;
            sessions_.erase(session);
            if (!stop_and_wait(*state->runtime))
            {
                return rejected(request.request_id, "runtime shutdown failed");
            }
            state->state.store(client::ClientRuntimeState::Stopped);
            return {ResponseType::Accepted, request.request_id, false, "disconnected"};
        }
        case RequestType::GetStatus:
        {
            if (session == sessions_.end())
            {
                return {ResponseType::Status, request.request_id, false, "disconnected"};
            }
            const auto state = session->second->state.load();
            return {ResponseType::Status,
                    request.request_id,
                    state == client::ClientRuntimeState::Connected,
                    status_message(state)};
        }
    }
    return rejected(request.request_id, "request type is unsupported");
}

void BrokerRuntimeRegistry::shutdown() noexcept
{
    std::vector<std::shared_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions.reserve(sessions_.size());
        for (auto& entry : sessions_)
        {
            sessions.push_back(std::move(entry.second));
        }
        sessions_.clear();
    }
    for (const auto& session : sessions)
    {
        if (session->runtime && stop_and_wait(*session->runtime))
        {
            session->state.store(client::ClientRuntimeState::Stopped);
        }
    }
}

} // namespace personal_vpn::broker
