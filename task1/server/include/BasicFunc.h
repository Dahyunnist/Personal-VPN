#ifndef BASICFUNC_H
#define BASICFUNC_H

#include <iostream>
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

namespace ssl = boost::asio::ssl;

uint32_t ip_to_uint(const std::string& ip_str);

std::string uint_to_ip(uint32_t ip);

std::string get_vm_ip();

// === SSL上下文配置函数 ===
ssl::context create_ssl_context();




#endif