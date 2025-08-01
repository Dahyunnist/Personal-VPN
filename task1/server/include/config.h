#ifndef CONFIG_H
#define CONFIG_H

// #include <iostream>
#include <string>
// #include <vector>
// #include <memory>
// #include <thread>
// #include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
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
#include <atomic>
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
// #include <fstream>
// #include <unordered_set>
#include <mutex>
// #include <nlohmann/json.hpp>

class IpPoolManager;


namespace vpn_config{
    const std::string TUN_DEV = "tun0";
    const std::string SERVER_TUN_IP = "10.8.0.1";
    const std::string TUN_MASK = "24";
    const std::string TUN_NETWORK = "10.8.0.0/24";
    const std::string PHYSICAL_NIC = "ens33";
    const size_t BUFFER_SIZE = 4096;
    const std::string CERTIFICATE_PATH = "certs/server.crt";
    const std::string PRIVATE_KEY_PATH = "certs/server.key";
    const size_t CLIENT_IP_LENGTH = 16;

    const std::string CLIENT_CONFIG_DIR = "client_config";
    const std::string CLIENT_CRT_PATH = CLIENT_CONFIG_DIR + "/client.crt";
    const std::string CLIENT_KEY_PATH = CLIENT_CONFIG_DIR + "/client.key";
    const std::string CLIENT_PFX_PATH = CLIENT_CONFIG_DIR + "/client.pfx";
    const std::string CONFIG_TXT_PATH = CLIENT_CONFIG_DIR + "/client.txt";
    const std::string PFX_PASSWORD = "123456";

    const std::string IP_POOL_START = "10.8.0.2";
    const std::string IP_POOL_END = "10.8.0.254";
}

namespace vpn_global{
    extern unsigned short port;
    extern std::atomic<bool> running;
    extern std::mutex cmd_mutex;
    extern IpPoolManager ip_pool;
}

#endif