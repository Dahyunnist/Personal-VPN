#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H


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
// #include <fstream>
// #include <unordered_set>
// #include <mutex>
// #include <nlohmann/json.hpp>

#include "config.h"


class SystemConfig {
public:
    static void run_command(const std::string& cmd);

    static void configure_tun(const std::string& dev, const std::string& ip, const std::string& mask);

    static void enable_ip_forward();

    static void setup_iptables_nat(const std::string& network, const std::string& nic);
};


#endif