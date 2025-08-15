#include "IpPoolManager.h"
// #include "config.h"
// #include <iostream>
// #include <string>
// #include <vector>
// #include <memory>
// #include <thread>
// #include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
// #include <boost/bind/bind.hpp>
// #include <sys/types.h>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
// #include <linux/if_tun.h>
// #include <unistd.h>
// #include <cstring>
// #include <array>
// #include <atomic>
// #include <csignal>
// #include <cstdlib>
// #include <arpa/inet.h>
// #include <boost/asio.hpp>
// #include <boost/asio/ssl.hpp>
// #include <sys/stat.h>
// #include <fcntl.h>
// #include <sys/ioctl.h>
// #include <net/if.h>
// #include <linux/if_tun.h>
// #include <unistd.h>
// #include <cstring>
#include <fstream>
// #include <unordered_set>
// #include <mutex>
#include <nlohmann/json.hpp>
#include "../include/BasicFunc.h"

IpPoolManager::IpPoolManager(const std::string& start, const std::string& end) : start_ip(start), end_ip(end) {}

void IpPoolManager::mark_ip_used(const std::string& ip)
{
    std::lock_guard<std::mutex> lock(ip_mutex);
    used_ips.insert(ip);
}

void IpPoolManager::release_ip(const std::string& ip)
{
    std::lock_guard<std::mutex> lock(ip_mutex);
    used_ips.erase(ip);
}

std::string IpPoolManager::get_available_ip()
{
    std::lock_guard<std::mutex> lock(ip_mutex);
    uint32_t start = ip_to_uint(start_ip);
    uint32_t end = ip_to_uint(end_ip);
    for (uint32_t ip = start; ip <= end; ip++)
    {
        std::string ip_str = uint_to_ip(ip);
        if (used_ips.find(ip_str) == used_ips.end())
        {
            return ip_str;
        }
    }
    throw std::runtime_error("No available IP in pool");
}

void IpPoolManager::generate_config_file()
{
    try
    {
        std::string available_ip = get_available_ip();
        auto read_file = [](const std::string& path)
        {
            std::ifstream file(path);
            if (!file)
            {
                throw std::runtime_error("Cannot open file: " + path);
            }
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        };
        nlohmann::json config = {
            {"server", {{"ip", get_vm_ip()}, {"port", vpn_global::port}}},
            {"tun", {{"ip", available_ip}, {"mask", vpn_config::TUN_MASK}}},
            {"certs", {{"client_crt", read_file(vpn_config::CLIENT_CRT_PATH)}, {"client_key", read_file(vpn_config::CLIENT_KEY_PATH)}, {"server_crt", read_file(vpn_config::CERTIFICATE_PATH)}}}};

        std::ofstream out("config.json");
        out << config.dump(4);
        std::cout << "Generated config.json with IP: " << available_ip << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to generate config: " << e.what() << std::endl;
    }
}

size_t IpPoolManager::get_available_ip_size()
{
    size_t size = 252 - used_ips.size();
    return size;
}