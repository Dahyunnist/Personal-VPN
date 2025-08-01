#ifndef IPPOOLMANAGER_H
#define IPPOOLMANAGER_H

#include "config.h"
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
#include <unordered_set>
// #include <mutex>
// #include <nlohmann/json.hpp>
// #include "BasicFunc.h"



class IpPoolManager{
public:
    IpPoolManager(const std::string& start, const std::string&end);

    void mark_ip_used(const std::string& ip);

    void release_ip(const std::string& ip);

    std::string get_available_ip();

    void generate_config_file();

    size_t get_available_ip_size();

private:
    std::unordered_set<std::string> used_ips;
    std::mutex ip_mutex;
    const std::string start_ip;
    const std::string end_ip;
};

#endif