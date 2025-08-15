#ifndef SESSION_H
#define SESSION_H

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

#include "TunDevice.h"
#include "config.h"
class Server;

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session>
{
   public:
    Session(ssl::stream<tcp::socket> socket, TunDevice& tun, Server& server);
    void start();
    void stop();
    bool is_active() const;
    // 写入数据到VPN隧道（发送给客户端）
    void async_write(const uint8_t* data, size_t size);
    const std::string& client_ip() const;

   private:
    // TLS握手
    void do_handshake();
    void handle_assigned_ip(const std::string& assigned_ip);
    // 从VPN隧道读取数据（客户端发来的）
    void start_reading();

    ssl::stream<tcp::socket> socket_;    // SSL加密流
    TunDevice& tun_;
    class Server& server_;
    std::array<uint8_t, vpn_config::BUFFER_SIZE> buffer_;
    std::atomic<bool> active_;
    std::string client_ip_;
    friend class Server;
};

#endif