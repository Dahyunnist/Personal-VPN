#include "personal_vpn/lease_manager.hpp"
#include "personal_vpn/linux_tun_device.hpp"
#include "personal_vpn/server_config.hpp"
#include "personal_vpn/tls_security.hpp"
#include "personal_vpn/tls_tunnel_server.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
using personal_vpn::core::LeaseManager;
using personal_vpn::core::parse_ipv4_address;
using personal_vpn::server::LinuxTunDevice;
using personal_vpn::server::ServerTlsConfig;
using personal_vpn::server::TlsTunnelServer;
using personal_vpn::server::make_server_tls_context;
using personal_vpn::server::parse_server_arguments;
using personal_vpn::server::server_usage;
using personal_vpn::server::validate_server_private_key_permissions;

void schedule_metrics(const std::shared_ptr<boost::asio::steady_timer>& timer,
                      const std::weak_ptr<TlsTunnelServer>& weak_server,
                      const std::chrono::seconds interval)
{
    if (interval == std::chrono::seconds::zero())
    {
        return;
    }
    timer->expires_after(interval);
    timer->async_wait(
        [timer, weak_server, interval](const boost::system::error_code& error)
        {
            if (error)
            {
                return;
            }
            if (const auto server = weak_server.lock())
            {
                const auto metrics = server->metrics_snapshot();
                std::cout << "{\"event\":\"server_metrics\",\"accepted\":"
                          << metrics.accepted_connections << ",\"capacity_rejections\":"
                          << metrics.capacity_rejections << ",\"handshake_failures\":"
                          << metrics.handshake_failures << ",\"handshake_timeouts\":"
                          << metrics.handshake_timeouts << ",\"idle_timeouts\":"
                          << metrics.idle_timeouts << ",\"established\":"
                          << metrics.established_sessions << ",\"active\":"
                          << server->active_session_count() << ",\"uplink_packets\":"
                          << metrics.uplink_packets << ",\"downlink_packets\":"
                          << metrics.downlink_packets << "}\n";
                schedule_metrics(timer, weak_server, interval);
            }
        });
}

int run_server(const personal_vpn::server::ServerConfig& config)
{
    boost::asio::io_context io_context;
    validate_server_private_key_permissions(config.private_key_file);
    auto tls_context = make_server_tls_context(
        ServerTlsConfig{config.certificate_chain_file,
                        config.private_key_file,
                        config.client_ca_file,
                        config.client_crl_file});
    LeaseManager lease_manager(parse_ipv4_address(config.first_lease_address),
                               parse_ipv4_address(config.last_lease_address),
                               parse_ipv4_address(config.gateway_address),
                               config.prefix_length,
                               config.mtu,
                               std::chrono::seconds(config.lease_seconds));
    auto tun_device = LinuxTunDevice::open(io_context, config.tun_name);
    const auto listen_address = boost::asio::ip::make_address_v4(config.listen_address);
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    auto metrics_timer = std::make_shared<boost::asio::steady_timer>(io_context);
    auto server = std::make_shared<TlsTunnelServer>(
        io_context,
        TlsTunnelServer::Tcp::endpoint(listen_address, config.listen_port),
        tls_context,
        lease_manager,
        tun_device,
        config.maximum_sessions,
        [&signals, metrics_timer]
        {
            boost::system::error_code ignored;
            signals.cancel(ignored);
            metrics_timer->cancel(ignored);
        },
        std::chrono::seconds(config.handshake_timeout_seconds),
        std::chrono::seconds(config.idle_timeout_seconds));

    signals.async_wait(
        [server](const boost::system::error_code& error, const int)
        {
            if (!error)
            {
                server->stop();
            }
        });
    server->start();
    schedule_metrics(metrics_timer,
                     server,
                     std::chrono::seconds(config.metrics_interval_seconds));
    std::cout << "Personal-VPN server listening on " << config.listen_address << ':'
              << config.listen_port << " using TUN " << tun_device->name() << '\n';

    std::atomic<bool> worker_failed{false};
    auto worker = [&io_context, &worker_failed]
    {
        try
        {
            io_context.run();
        }
        catch (const std::exception& error)
        {
            worker_failed.store(true);
            std::cerr << "I/O worker failed: " << error.what() << '\n';
            io_context.stop();
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(config.worker_threads - 1U);
    for (std::size_t index = 1U; index < config.worker_threads; ++index)
    {
        threads.emplace_back(worker);
    }
    worker();
    for (auto& thread : threads)
    {
        thread.join();
    }
    return worker_failed.load() ? EXIT_FAILURE : EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        const std::vector<std::string> arguments(argv + 1, argv + argc);
        const auto config = parse_server_arguments(arguments);
        if (config.show_help)
        {
            std::cout << server_usage(argc > 0 ? argv[0] : "personal-vpn-server");
            return EXIT_SUCCESS;
        }
        return run_server(config);
    }
    catch (const std::exception& error)
    {
        std::cerr << "personal-vpn-server: " << error.what() << "\n\n"
                  << server_usage(argc > 0 ? argv[0] : "personal-vpn-server");
        return EXIT_FAILURE;
    }
}
